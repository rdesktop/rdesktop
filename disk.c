/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Disk Redirection
   Copyright (C) Jeroen Meijer <jeroen@oldambt7.com> 2003-2008
   Copyright 2003-2011 Peter Astrand <astrand@cendio.se> for Cendio AB
   Copyright 2017 Henrik Andersson <hean01@cendio.se> for Cendio AB
   Copyright 2017 Karl Mikaelsson <derfian@cendio.se> for Cendio AB
   Copyright 2017 Alexander Zakharov <uglym8@gmail.com>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "disk.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>		/* open, close */
#include <dirent.h>		/* opendir, closedir, readdir */
#include <fnmatch.h>
#include <errno.h>		/* errno */
#include <stdio.h>

#include <utime.h>
#include <time.h>		/* ctime */

#if (defined(HAVE_DIRFD) || (HAVE_DECL_DIRFD == 1))
#define DIRFD(a) (dirfd(a))
#else
#define DIRFD(a) ((a)->DIR_FD_MEMBER_NAME)
#endif

/* TODO: Fix mntent-handling for solaris
 * #include <sys/mntent.h> */
#if (defined(HAVE_MNTENT_H) && defined(HAVE_SETMNTENT))
#include <mntent.h>
#define MNTENT_PATH "/etc/mtab"
#define USE_SETMNTENT
#endif

#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif

#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif

#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#include "rdesktop.h"

#ifdef STAT_STATFS3_OSF1
#define STATFS_FN(path, buf) (statfs(path,buf,sizeof(buf)))
#define STATFS_T statfs
#define USE_STATFS
#endif

#ifdef STAT_STATVFS
#define STATFS_FN(path, buf) (statvfs(path,buf))
#define STATFS_T statvfs
#define USE_STATVFS
#endif

#ifdef STAT_STATVFS64
#define STATFS_FN(path, buf) (statvfs64(path,buf))
#define STATFS_T statvfs64
#define USE_STATVFS
#endif

#if (defined(STAT_STATFS2_FS_DATA) || defined(STAT_STATFS2_BSIZE) || defined(STAT_STATFS2_FSIZE))
#define STATFS_FN(path, buf) (statfs(path,buf))
#define STATFS_T statfs
#define USE_STATFS
#endif

#ifdef STAT_STATFS4
#define STATFS_FN(path, buf) (statfs(path,buf,sizeof(buf),0))
#define STATFS_T statfs
#define USE_STATFS
#endif

#if ((defined(USE_STATFS) && defined(HAVE_STRUCT_STATFS_F_NAMEMAX)) || (defined(USE_STATVFS) && defined(HAVE_STRUCT_STATVFS_F_NAMEMAX)))
#define F_NAMELEN(buf) ((buf).f_namemax)
#endif

#if ((defined(USE_STATFS) && defined(HAVE_STRUCT_STATFS_F_NAMELEN)) || (defined(USE_STATVFS) && defined(HAVE_STRUCT_STATVFS_F_NAMELEN)))
#define F_NAMELEN(buf) ((buf).f_namelen)
#endif

#ifndef F_NAMELEN
#define F_NAMELEN(buf) (255)
#endif

/* Dummy statfs fallback */
#ifndef STATFS_T
struct dummy_statfs_t
{
	long f_bfree;
	long f_bsize;
	long f_bavail;
	long f_blocks;
	int f_namelen;
	int f_namemax;
};

static int
dummy_statfs(struct dummy_statfs_t *buf)
{
	buf->f_blocks = 262144;
	buf->f_bfree = 131072;
	buf->f_bavail = 131072;
	buf->f_bsize = 512;
	buf->f_namelen = 255;
	buf->f_namemax = 255;

	return 0;
}

#define STATFS_T dummy_statfs_t
#define STATFS_FN(path,buf) (dummy_statfs(buf))
#endif

extern RDPDR_DEVICE g_rdpdr_device[];

FILEINFO g_fileinfo[MAX_OPEN_FILES];
RD_BOOL g_notify_stamp = False;

typedef struct
{
	char name[PATH_MAX];
	char label[PATH_MAX];
	unsigned long serial;
	char type[PATH_MAX];
} FsInfoType;

static RD_NTSTATUS NotifyInfo(RD_NTHANDLE handle, uint32 info_class, NOTIFY * p);

static time_t
get_create_time(struct stat *filestat)
{
	time_t ret, ret1;

	ret = MIN(filestat->st_ctime, filestat->st_mtime);
	ret1 = MIN(ret, filestat->st_atime);

	if (ret1 != (time_t) 0)
		return ret1;

	return ret;
}

/* Convert seconds since 1970 to a filetime */
static void
seconds_since_1970_to_filetime(time_t seconds, uint32 * high, uint32 * low)
{
	unsigned long long ticks;

	ticks = (seconds + 11644473600LL) * 10000000;
	*low = (uint32) ticks;
	*high = (uint32) (ticks >> 32);
}

/* Convert seconds since 1970 back to filetime */
static time_t
convert_1970_to_filetime(uint32 high, uint32 low)
{
	unsigned long long ticks;
	time_t val;

	ticks = low + (((unsigned long long) high) << 32);
	ticks /= 10000000;
	ticks -= 11644473600LL;

	val = (time_t) ticks;
	return (val);

}

/* A wrapper for ftruncate which supports growing files, even if the
   native ftruncate doesn't. This is needed on Linux FAT filesystems,
   for example. */
static int
ftruncate_growable(int fd, off_t length)
{
	int ret;
	off_t pos;
	static const char zero = 0;

	/* Try the simple method first */
	if ((ret = ftruncate(fd, length)) != -1)
	{
		return ret;
	}

	/*
	 * Some kind of error. Perhaps we were trying to grow. Retry
	 * in a safe way.
	 */

	/* Get current position */
	if ((pos = lseek(fd, 0, SEEK_CUR)) == -1)
	{
		logger(Disk, Error, "ftruncate_growable(), lseek() failed: %s", strerror(errno));
		return -1;
	}

	/* Seek to new size */
	if (lseek(fd, length, SEEK_SET) == -1)
	{
		logger(Disk, Error, "ftruncate_growable(), lseek() failed: %s", strerror(errno));
		return -1;
	}

	/* Write a zero */
	if (write(fd, &zero, 1) == -1)
	{
		logger(Disk, Error, "ftruncate_growable(), write() failed: %s", strerror(errno));
		return -1;
	}

	/* Truncate. This shouldn't fail. */
	if (ftruncate(fd, length) == -1)
	{
		logger(Disk, Error, "ftruncate_growable(), ftruncate() failed: %s",
		       strerror(errno));
		return -1;
	}

	/* Restore position */
	if (lseek(fd, pos, SEEK_SET) == -1)
	{
		logger(Disk, Error, "ftruncate_growable(), lseek() failed: %s", strerror(errno));
		return -1;
	}

	return 0;
}

/* Just like open(2), but if a open with O_EXCL fails, retry with
   GUARDED semantics. This might be necessary because some filesystems
   (such as NFS filesystems mounted from a unfsd server) doesn't
   support O_EXCL. GUARDED semantics are subject to race conditions,
   but we can live with that.
*/
static int
open_weak_exclusive(const char *pathname, int flags, mode_t mode)
{
	int ret;
	struct stat filestat;

	ret = open(pathname, flags, mode);
	if (ret != -1 || !(flags & O_EXCL))
	{
		/* Success, or not using O_EXCL */
		return ret;
	}

	/* An error occurred, and we are using O_EXCL. In case the FS
	   doesn't support O_EXCL, some kind of error will be
	   returned. Unfortunately, we don't know which one. Linux
	   2.6.8 seems to return 524, but I cannot find a documented
	   #define for this case. So, we'll return only on errors that
	   we know aren't related to O_EXCL. */
	switch (errno)
	{
		case EACCES:
		case EEXIST:
		case EINTR:
		case EISDIR:
		case ELOOP:
		case ENAMETOOLONG:
		case ENOENT:
		case ENOTDIR:
			return ret;
	}

	/* Retry with GUARDED semantics */
	if (stat(pathname, &filestat) != -1)
	{
		/* File exists */
		errno = EEXIST;
		return -1;
	}
	else
	{
		return open(pathname, flags & ~O_EXCL, mode);
	}
}

/* Enumeration of devices from rdesktop.c        */
/* returns number of units found and initialized. */
/* optarg looks like ':h=/mnt/floppy,b=/mnt/usbdevice1' */
/* when it arrives to this function.             */
int
disk_enum_devices(uint32 * id, char *optarg)
{
	char *pos = optarg;
	char *pos2;
	int count = 0;
	DISK_DEVICE *pdisk_data;

	/* skip the first colon */
	optarg++;
	while ((pos = next_arg(optarg, ',')) && *id < RDPDR_MAX_DEVICES)
	{
		pos2 = next_arg(optarg, '=');

		pdisk_data = (DISK_DEVICE *) xmalloc(sizeof(DISK_DEVICE));
		memset(pdisk_data, 0, sizeof(DISK_DEVICE));
		strncpy(pdisk_data->name, optarg, sizeof(pdisk_data->name) - 1);
		strncpy(g_rdpdr_device[*id].name, optarg, sizeof(g_rdpdr_device[*id].name) - 1);

		g_rdpdr_device[*id].local_path = (char *) xmalloc(strlen(pos2) + 1);
		strcpy(g_rdpdr_device[*id].local_path, pos2);
		g_rdpdr_device[*id].device_type = DEVICE_TYPE_DISK;
		g_rdpdr_device[*id].pdevice_data = (void *) pdisk_data;

		count++;
		(*id)++;

		optarg = pos;
	}
	return count;
}

/* Opens or creates a file or directory */
static RD_NTSTATUS
disk_create(uint32 device_id, uint32 accessmask, uint32 sharemode, uint32 create_disposition,
	    uint32 flags_and_attributes, char *filename, RD_NTHANDLE * phandle)
{
	int handle;
	DIR *dirp;
	int flags, mode;
	char path[PATH_MAX];
	struct stat filestat;

	logger(Disk, Debug, "disk_create(device_id=0x%x, accessmask=0x%x, sharemode=0x%x, "
	       "create_disp=%d, flags=0x%x, fname=%s, ...)", device_id, accessmask,
	       sharemode, create_disposition, flags_and_attributes, filename);
	handle = 0;
	dirp = NULL;
	flags = 0;
	mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	if (filename && *filename && filename[strlen(filename) - 1] == '/')
		filename[strlen(filename) - 1] = 0;

	sprintf(path, "%s%s", g_rdpdr_device[device_id].local_path, filename ? filename : "");

	/* Protect against malicious servers:
	   somelongpath/..     not allowed
	   somelongpath/../b   not allowed
	   somelongpath/..b    in principle ok, but currently not allowed
	   somelongpath/b..    ok
	   somelongpath/b..b   ok
	   somelongpath/b../c  ok
	 */
	if (strstr(path, "/.."))
	{
		return RD_STATUS_ACCESS_DENIED;
	}

	switch (create_disposition)
	{
		case CREATE_ALWAYS:

			/* Delete existing file/link. */
			unlink(path);
			flags |= O_CREAT;
			break;

		case CREATE_NEW:

			/* If the file already exists, then fail. */
			flags |= O_CREAT | O_EXCL;
			break;

		case OPEN_ALWAYS:

			/* Create if not already exists. */
			flags |= O_CREAT;
			break;

		case OPEN_EXISTING:

			/* Default behaviour */
			break;

		case TRUNCATE_EXISTING:

			/* If the file does not exist, then fail. */
			flags |= O_TRUNC;
			break;
	}

	/*printf("Open: \"%s\"  flags: %X, accessmask: %X sharemode: %X create disp: %X\n", path, flags_and_attributes, accessmask, sharemode, create_disposition); */

	/* Get information about file and set that flag ourselves */
	if ((stat(path, &filestat) == 0) && (S_ISDIR(filestat.st_mode)))
	{
		if (flags_and_attributes & FILE_NON_DIRECTORY_FILE)
			return RD_STATUS_FILE_IS_A_DIRECTORY;
		else
			flags_and_attributes |= FILE_DIRECTORY_FILE;
	}

	if (flags_and_attributes & FILE_DIRECTORY_FILE)
	{
		if (flags & O_CREAT)
		{
			mkdir(path, mode);
		}

		dirp = opendir(path);
		if (!dirp)
		{
			switch (errno)
			{
				case EACCES:

					return RD_STATUS_ACCESS_DENIED;

				case ENOENT:

					return RD_STATUS_NO_SUCH_FILE;

				default:
					logger(Disk, Error, "disk_create(), opendir() failed: %s",
					       strerror(errno));
					return RD_STATUS_NO_SUCH_FILE;
			}
		}
		handle = DIRFD(dirp);
	}
	else
	{

		if (accessmask & GENERIC_ALL
		    || (accessmask & GENERIC_READ && accessmask & GENERIC_WRITE))
		{
			flags |= O_RDWR;
		}
		else if ((accessmask & GENERIC_WRITE) && !(accessmask & GENERIC_READ))
		{
			flags |= O_WRONLY;
		}
		else
		{
			flags |= O_RDONLY;
		}

		handle = open_weak_exclusive(path, flags, mode);
		if (handle == -1)
		{
			switch (errno)
			{
				case EISDIR:

					return RD_STATUS_FILE_IS_A_DIRECTORY;

				case EACCES:

					return RD_STATUS_ACCESS_DENIED;

				case ENOENT:

					return RD_STATUS_NO_SUCH_FILE;
				case EEXIST:

					return RD_STATUS_OBJECT_NAME_COLLISION;
				default:
					logger(Disk, Error, "disk_create(), open() failed: %s",
					       strerror(errno));
					return RD_STATUS_NO_SUCH_FILE;
			}
		}

		/* all read and writes of files should be non blocking */
		if (fcntl(handle, F_SETFL, O_NONBLOCK) == -1)
			logger(Disk, Error, "disk_create(), fcntl() failed: %s", strerror(errno));

	}

	if (handle >= MAX_OPEN_FILES)
	{
		logger(Disk, Error,
		       "disk_create(), handle (%d) is greater than or equal to MAX_OPEN_FILES (%d)!",
		       handle, MAX_OPEN_FILES);
		exit(EX_SOFTWARE);
	}

	if (dirp)
		g_fileinfo[handle].pdir = dirp;
	else
		g_fileinfo[handle].pdir = NULL;

	g_fileinfo[handle].device_id = device_id;
	g_fileinfo[handle].flags_and_attributes = flags_and_attributes;
	g_fileinfo[handle].accessmask = accessmask;
	strncpy(g_fileinfo[handle].path, path, PATH_MAX - 1);
	g_fileinfo[handle].delete_on_close = False;

	if (accessmask & GENERIC_ALL || accessmask & GENERIC_WRITE)
		g_notify_stamp = True;

	*phandle = handle;
	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
disk_close(RD_NTHANDLE handle)
{
	struct fileinfo *pfinfo;

	logger(Disk, Debug, "disk_close(handle=0x%x)", handle);

	pfinfo = &(g_fileinfo[handle]);

	if (pfinfo->accessmask & GENERIC_ALL || pfinfo->accessmask & GENERIC_WRITE)
		g_notify_stamp = True;

	rdpdr_abort_io(handle, 0, RD_STATUS_CANCELLED);

	if (pfinfo->pdir)
	{
		if (closedir(pfinfo->pdir) < 0)
		{
			logger(Disk, Error, "disk_close(), closedir() failed: %s", strerror(errno));
			return RD_STATUS_INVALID_HANDLE;
		}

		if (pfinfo->delete_on_close)
			if (rmdir(pfinfo->path) < 0)
			{
				logger(Disk, Error, "disk_close(), rmdir() failed: %s",
				       strerror(errno));
				return RD_STATUS_ACCESS_DENIED;
			}
		pfinfo->delete_on_close = False;
	}
	else
	{
		if (close(handle) < 0)
		{
			logger(Disk, Error, "disk_close(), close() failed: %s", strerror(errno));
			return RD_STATUS_INVALID_HANDLE;
		}
		if (pfinfo->delete_on_close)
			if (unlink(pfinfo->path) < 0)
			{
				logger(Disk, Error, "disk_close(), unlink() failed: %s",
				       strerror(errno));
				return RD_STATUS_ACCESS_DENIED;
			}

		pfinfo->delete_on_close = False;
	}

	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
disk_read(RD_NTHANDLE handle, uint8 * data, uint32 length, uint64 offset, uint32 * result)
{
	int n;

#if 0
	/* browsing dir ????        */
	/* each request is 24 bytes */
	if (g_fileinfo[handle].flags_and_attributes & FILE_DIRECTORY_FILE)
	{
		*result = 0;
		return STATUS_SUCCESS;
	}
#endif

	lseek(handle, offset, SEEK_SET);

	n = read(handle, data, length);

	if (n < 0)
	{
		*result = 0;
		switch (errno)
		{
			case EISDIR:
				/* Implement 24 Byte directory read ??
				   with STATUS_NOT_IMPLEMENTED server doesn't read again */
				/* return STATUS_FILE_IS_A_DIRECTORY; */
				return RD_STATUS_NOT_IMPLEMENTED;
			default:
				logger(Disk, Error, "disk_read(), read failed: %s",
				       strerror(errno));
				return RD_STATUS_INVALID_PARAMETER;
		}
	}

	*result = n;

	return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS
disk_write(RD_NTHANDLE handle, uint8 * data, uint32 length, uint64 offset, uint32 * result)
{
	int n;

	lseek(handle, offset, SEEK_SET);

	n = write(handle, data, length);

	if (n < 0)
	{
		logger(Disk, Error, "disk_write(), write() failed: %s", strerror(errno));
		*result = 0;
		switch (errno)
		{
			case ENOSPC:
				return RD_STATUS_DISK_FULL;
			default:
				return RD_STATUS_ACCESS_DENIED;
		}
	}

	*result = n;

	return RD_STATUS_SUCCESS;
}

/* Btw, all used Flie* structures are described in [MS-FSCC] */
RD_NTSTATUS
disk_query_information(RD_NTHANDLE handle, uint32 info_class, STREAM out)
{
	uint32 file_attributes, ft_high, ft_low;
	struct stat filestat;
	char *path, *filename;

	logger(Disk, Debug, "disk_query_information(handle=0x%x, info_class=0x%x)", handle,
	       info_class);

	path = g_fileinfo[handle].path;

	/* Get information about file */
	if (fstat(handle, &filestat) != 0)
	{
		logger(Disk, Error, "disk_query_information(), stat() failed: %s", strerror(errno));
		out_uint8(out, 0);
		return RD_STATUS_ACCESS_DENIED;
	}

	/* Set file attributes */
	file_attributes = 0;
	if (S_ISDIR(filestat.st_mode))
		file_attributes |= FILE_ATTRIBUTE_DIRECTORY;

	filename = strrchr(path, '/');
	if (filename)
		filename += 1;

	if (filename && filename[0] == '.')
		file_attributes |= FILE_ATTRIBUTE_HIDDEN;

	if (!file_attributes)
		file_attributes |= FILE_ATTRIBUTE_NORMAL;

	if (!(filestat.st_mode & S_IWUSR))
		file_attributes |= FILE_ATTRIBUTE_READONLY;

	/* Return requested data */
	switch (info_class)
	{
		case FileBasicInformation:
			seconds_since_1970_to_filetime(get_create_time(&filestat), &ft_high,
						       &ft_low);
			out_uint32_le(out, ft_low);	/* create_access_time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_atime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* last_access_time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_mtime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* last_write_time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_ctime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* last_change_time */
			out_uint32_le(out, ft_high);

			out_uint32_le(out, file_attributes);
			break;

		case FileStandardInformation:

			out_uint64_le(out, filestat.st_size);	/* Allocation size */
			out_uint64_le(out, filestat.st_size);	/* End of file */

			out_uint32_le(out, filestat.st_nlink);	/* Number of links */
			out_uint8(out, 0);	/* Delete pending */
			out_uint8(out, S_ISDIR(filestat.st_mode) ? 1 : 0);	/* Directory */
			break;

		case FileObjectIdInformation:

			out_uint32_le(out, file_attributes);	/* File Attributes */
			out_uint32_le(out, 0);	/* Reparse Tag */
			break;

		default:
			logger(Disk, Warning,
			       "disk_query_information(), unhandled query information class 0x%x",
			       info_class);
			return RD_STATUS_INVALID_PARAMETER;
	}
	return RD_STATUS_SUCCESS;
}

/* 2.2.3.3.9 [MS-RDPEFS] */
RD_NTSTATUS
disk_set_information(RD_NTHANDLE handle, uint32 info_class, STREAM in, STREAM out)
{
	UNUSED(out);
	uint32 length, file_attributes, ft_high, ft_low;
	char *newname, fullpath[PATH_MAX];
	struct fileinfo *pfinfo;
	int mode;
	struct stat filestat;
	time_t write_time, change_time, access_time, mod_time;
	struct utimbuf tvs;
	struct STATFS_T stat_fs;

	logger(Disk, Debug, "disk_set_information(handle=0x%x, info_class=0x%x, ...)", handle,
	       info_class);

	pfinfo = &(g_fileinfo[handle]);
	g_notify_stamp = True;
	newname = NULL;

	switch (info_class)
	{
		case FileBasicInformation:
			write_time = change_time = access_time = 0;

			in_uint8s(in, 4);	/* length of SetBuffer */
			in_uint8s(in, 24);	/* padding */

			/* CreationTime */
			in_uint32_le(in, ft_low);
			in_uint32_le(in, ft_high);

			/* AccessTime */
			in_uint32_le(in, ft_low);
			in_uint32_le(in, ft_high);
			if (ft_low || ft_high)
				access_time = convert_1970_to_filetime(ft_high, ft_low);

			/* WriteTime */
			in_uint32_le(in, ft_low);
			in_uint32_le(in, ft_high);
			if (ft_low || ft_high)
				write_time = convert_1970_to_filetime(ft_high, ft_low);

			/* ChangeTime */
			in_uint32_le(in, ft_low);
			in_uint32_le(in, ft_high);
			if (ft_low || ft_high)
				change_time = convert_1970_to_filetime(ft_high, ft_low);

			in_uint32_le(in, file_attributes);

			if (fstat(handle, &filestat))
				return RD_STATUS_ACCESS_DENIED;

			tvs.modtime = filestat.st_mtime;
			tvs.actime = filestat.st_atime;
			if (access_time)
				tvs.actime = access_time;


			if (write_time || change_time)
				mod_time = MIN(write_time, change_time);
			else
				mod_time = write_time ? write_time : change_time;

			if (mod_time)
				tvs.modtime = mod_time;


			if (access_time || write_time || change_time)
			{
				logger(Disk, Debug,
				       "disk_set_information(), access time='%s', modify time='%s'",
				       ctime(&tvs.actime), ctime(&tvs.modtime));
				if (utime(pfinfo->path, &tvs) && errno != EPERM)
					return RD_STATUS_ACCESS_DENIED;
			}

			if (!file_attributes)
				break;	/* not valid */

			mode = filestat.st_mode;

			if (file_attributes & FILE_ATTRIBUTE_READONLY)
				mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
			else
				mode |= S_IWUSR;

			mode &= 0777;

			logger(Disk, Debug, "disk_set_information(), access mode 0%o", mode);

			if (fchmod(handle, mode))
				return RD_STATUS_ACCESS_DENIED;

			break;

		case FileRenameInformation:

			in_uint8s(in, 4);	/* Handle of root dir? */
			in_uint8s(in, 0x1a);	/* unknown */
			in_uint32_le(in, length);

			if (length && (length / 2) >= 256)
				return RD_STATUS_INVALID_PARAMETER;

			rdp_in_unistr(in, length, &newname, &length);
			if (newname == NULL)
				return RD_STATUS_INVALID_PARAMETER;

			convert_to_unix_filename(newname);

			sprintf(fullpath, "%s%s", g_rdpdr_device[pfinfo->device_id].local_path,
				newname);

			free(newname);

			if (rename(pfinfo->path, fullpath) != 0)
			{
				logger(Disk, Error, "disk_set_information(), rename() failed: %s",
				       strerror(errno));
				return RD_STATUS_ACCESS_DENIED;
			}
			break;

		case FileDispositionInformation:
			/* As far as I understand it, the correct
			   thing to do here is to *schedule* a delete,
			   so it will be deleted when the file is
			   closed. Subsequent
			   FileDispositionInformation requests with
			   DeleteFile set to FALSE should unschedule
			   the delete. See
			   http://www.osronline.com/article.cfm?article=245. */

			/* FileDispositionInformation always sets delete_on_close to true.
			   "STREAM in" includes Length(4bytes) , Padding(24bytes) and SetBuffer(zero byte).
			   Length is always set to zero.
			   [MS-RDPEFS] http://msdn.microsoft.com/en-us/library/cc241305%28PROT.10%29.aspx
			   - 2.2.3.3.9 Server Drive Set Information Request
			 */
			in_uint8s(in, 4);	/* length of SetBuffer */
			in_uint8s(in, 24);	/* padding */


			if ((pfinfo->accessmask &
			     (FILE_DELETE_ON_CLOSE | FILE_COMPLETE_IF_OPLOCKED)))
			{
				/* if file exists in directory , necessary to return RD_STATUS_DIRECTORY_NOT_EMPTY with win2008
				   [MS-RDPEFS] http://msdn.microsoft.com/en-us/library/cc241305%28PROT.10%29.aspx
				   - 2.2.3.3.9 Server Drive Set Information Request
				   - 2.2.3.4.9 Client Drive Set Information Response
				   [MS-FSCC] http://msdn.microsoft.com/en-us/library/cc231987%28PROT.10%29.aspx
				   - 2.4.11 FileDispositionInformation
				   [FSBO] http://msdn.microsoft.com/en-us/library/cc246487%28PROT.13%29.aspx
				   - 4.3.2 Set Delete-on-close using FileDispositionInformation Information Class (IRP_MJ_SET_INFORMATION)
				 */
				if (pfinfo->pdir)
				{
					DIR *dp = opendir(pfinfo->path);
					struct dirent *dir;

					while ((dir = readdir(dp)) != NULL)
					{
						if (strcmp(dir->d_name, ".") != 0
						    && strcmp(dir->d_name, "..") != 0)
						{
							closedir(dp);
							return RD_STATUS_DIRECTORY_NOT_EMPTY;
						}
					}
					closedir(dp);
				}

				pfinfo->delete_on_close = True;
			}

			break;

		case FileAllocationInformation:
			/* Fall through to FileEndOfFileInformation,
			   which uses ftrunc. This is like Samba with
			   "strict allocation = false", and means that
			   we won't detect out-of-quota errors, for
			   example. */

		case FileEndOfFileInformation:
			in_uint8s(in, 28);	/* unknown */
			in_uint32_le(in, length);	/* file size */

			/* prevents start of writing if not enough space left on device */
			if (STATFS_FN(pfinfo->path, &stat_fs) == 0)
				if (stat_fs.f_bfree * stat_fs.f_bsize < length)
					return RD_STATUS_DISK_FULL;

			if (ftruncate_growable(handle, length) != 0)
			{
				return RD_STATUS_DISK_FULL;
			}

			break;
		default:
			logger(Disk, Warning,
			       "disk_set_information(), unhandled information class 0x%x",
			       info_class);
			return RD_STATUS_INVALID_PARAMETER;
	}
	return RD_STATUS_SUCCESS;
}

RD_NTSTATUS
disk_check_notify(RD_NTHANDLE handle)
{
	struct fileinfo *pfinfo;
	RD_NTSTATUS status = RD_STATUS_PENDING;
	NOTIFY notify;

	logger(Disk, Debug, "disk_check_notify(handle=0x%x)", handle);

	pfinfo = &(g_fileinfo[handle]);
	if (!pfinfo->pdir)
		return RD_STATUS_INVALID_DEVICE_REQUEST;



	status = NotifyInfo(handle, pfinfo->info_class, &notify);

	if (status != RD_STATUS_PENDING)
		return status;

	if (memcmp(&pfinfo->notify, &notify, sizeof(NOTIFY)))
	{
		/*printf("disk_check_notify found changed event\n"); */
		memcpy(&pfinfo->notify, &notify, sizeof(NOTIFY));
		status = RD_STATUS_NOTIFY_ENUM_DIR;
	}

	return status;


}

RD_NTSTATUS
disk_create_notify(RD_NTHANDLE handle, uint32 info_class)
{
	struct fileinfo *pfinfo;
	RD_NTSTATUS ret = RD_STATUS_PENDING;

	logger(Disk, Debug, "disk_create_notify(handle=0x%x, info_class=0x%x)", handle, info_class);

	pfinfo = &(g_fileinfo[handle]);
	pfinfo->info_class = info_class;

	ret = NotifyInfo(handle, info_class, &pfinfo->notify);

	if (info_class & 0x1000)
	{			/* ???? */
		if (ret == RD_STATUS_PENDING)
			return RD_STATUS_SUCCESS;
	}

	/* printf("disk_create_notify: num_entries %d\n", pfinfo->notify.num_entries); */


	return ret;

}

static RD_NTSTATUS
NotifyInfo(RD_NTHANDLE handle, uint32 info_class, NOTIFY * p)
{
	UNUSED(info_class);
	struct fileinfo *pfinfo;
	struct stat filestat;
	struct dirent *dp;
	char *fullname;
	DIR *dpr;

	pfinfo = &(g_fileinfo[handle]);
	if (fstat(handle, &filestat) < 0)
	{
		logger(Disk, Error, "NotifyInfo(), fstat failed: %s", strerror(errno));
		return RD_STATUS_ACCESS_DENIED;
	}
	p->modify_time = filestat.st_mtime;
	p->status_time = filestat.st_ctime;
	p->num_entries = 0;
	p->total_time = 0;


	dpr = opendir(pfinfo->path);
	if (!dpr)
	{
		logger(Disk, Error, "NotifyInfo(), opendir failed: %s", strerror(errno));
		return RD_STATUS_ACCESS_DENIED;
	}


	while ((dp = readdir(dpr)))
	{
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;
		p->num_entries++;
		fullname = (char *) xmalloc(strlen(pfinfo->path) + strlen(dp->d_name) + 2);
		sprintf(fullname, "%s/%s", pfinfo->path, dp->d_name);

		if (!stat(fullname, &filestat))
		{
			p->total_time += (filestat.st_mtime + filestat.st_ctime);
		}

		xfree(fullname);
	}
	closedir(dpr);

	return RD_STATUS_PENDING;
}

static FsInfoType *
FsVolumeInfo(char *fpath)
{

	static FsInfoType info;
#ifdef USE_SETMNTENT
	FILE *fdfs;
	struct mntent *e;
#endif

	/* initialize */
	memset(&info, 0, sizeof(info));
	strcpy(info.label, "RDESKTOP");
	strcpy(info.type, "RDPFS");

#ifdef USE_SETMNTENT
	fdfs = setmntent(MNTENT_PATH, "r");
	if (!fdfs)
		return &info;

	while ((e = getmntent(fdfs)))
	{
		if (str_startswith(e->mnt_dir, fpath))
		{
			strcpy(info.type, e->mnt_type);
			strcpy(info.name, e->mnt_fsname);
			if (strstr(e->mnt_opts, "vfat") || strstr(e->mnt_opts, "iso9660"))
			{
				int fd = open(e->mnt_fsname, O_RDONLY);
				if (fd >= 0)
				{
					unsigned char buf[512];
					memset(buf, 0, sizeof(buf));
					if (strstr(e->mnt_opts, "vfat"))
						 /*FAT*/
					{
						strcpy(info.type, "vfat");
						read(fd, buf, sizeof(buf));
						info.serial =
							(buf[42] << 24) + (buf[41] << 16) +
							(buf[40] << 8) + buf[39];
						strncpy(info.label, (char *) buf + 43, 10);
						info.label[10] = '\0';
					}
					else if (lseek(fd, 32767, SEEK_SET) >= 0)	/* ISO9660 */
					{
						read(fd, buf, sizeof(buf));
						strncpy(info.label, (char *) buf + 41, 32);
						info.label[32] = '\0';
						/* info.Serial = (buf[128]<<24)+(buf[127]<<16)+(buf[126]<<8)+buf[125]; */
					}
					close(fd);
				}
			}
		}
	}
	endmntent(fdfs);
#else
	/* initialize */
	memset(&info, 0, sizeof(info));
	strcpy(info.label, "RDESKTOP");
	strcpy(info.type, "RDPFS");

#endif
	return &info;
}


RD_NTSTATUS
disk_query_volume_information(RD_NTHANDLE handle, uint32 info_class, STREAM out)
{
	struct STATFS_T stat_fs;
	struct fileinfo *pfinfo;
	FsInfoType *fsinfo;
	STREAM stmp;

	logger(Disk, Debug, "disk_query_volume_information(handle=0x%x, info_class=0x%x)", handle,
	       info_class);

	pfinfo = &(g_fileinfo[handle]);

	if (STATFS_FN(pfinfo->path, &stat_fs) != 0)
	{
		logger(Disk, Error, "disk_query_volume_information(), statfs() failed: %s",
		       strerror(errno));
		return RD_STATUS_ACCESS_DENIED;
	}

	fsinfo = FsVolumeInfo(pfinfo->path);

	switch (info_class)
	{
		case FileFsVolumeInformation:
			stmp = s_alloc(PATH_MAX * 4);
			out_utf16s(stmp, fsinfo->label);
			s_mark_end(stmp);

			out_uint32_le(out, 0);	/* volume creation time low */
			out_uint32_le(out, 0);	/* volume creation time high */
			out_uint32_le(out, fsinfo->serial);	/* serial */
			out_uint32_le(out, s_length(stmp));	/* length of string */
			out_uint8(out, 0);	/* support objects? */
			out_stream(out, stmp);	/* fsinfo->label string */
			s_free(stmp);
			break;

		case FileFsSizeInformation:

			out_uint64_le(out, stat_fs.f_blocks);	/* Total allocation units */
			out_uint64_le(out, stat_fs.f_bfree);	/* Available allocation units */
			out_uint32_le(out, stat_fs.f_bsize / 0x200);	/* Sectors per allocation unit */
			out_uint32_le(out, 0x200);	/* Bytes per sector */
			break;

		case FileFsFullSizeInformation:

			out_uint64_le(out, stat_fs.f_blocks);	/* Total allocation units */
			out_uint64_le(out, stat_fs.f_bavail);	/* Caller allocation units */
			out_uint64_le(out, stat_fs.f_bfree);	/* Available allocation units */
			out_uint32_le(out, stat_fs.f_bsize / 0x200);	/* Sectors per allocation unit */
			out_uint32_le(out, 0x200);	/* Bytes per sector */
			break;

		case FileFsAttributeInformation:
			stmp = s_alloc(PATH_MAX * 4);
			out_utf16s_no_eos(stmp, fsinfo->type);
			s_mark_end(stmp);

			out_uint32_le(out, FS_CASE_SENSITIVE | FS_CASE_IS_PRESERVED);	/* fs attributes */
			out_uint32_le(out, F_NAMELEN(stat_fs));	/* max length of filename */

			out_uint32_le(out, s_length(stmp));	/* length of fsinfo->type string */
			out_stream(out, stmp);	/* fsinfo->typ string */
			s_free(stmp);
			break;

		case FileFsLabelInformation:
		case FileFsDeviceInformation:
		case FileFsControlInformation:
		case FileFsObjectIdInformation:
		case FileFsMaximumInformation:

		default:
			logger(Disk, Warning,
			       "disk_query_volume_information(), unhandled volume info class 0x%x",
			       info_class);
			return RD_STATUS_INVALID_PARAMETER;
	}
	return RD_STATUS_SUCCESS;
}

RD_NTSTATUS
disk_query_directory(RD_NTHANDLE handle, uint32 info_class, char *pattern, STREAM out)
{
	uint32 file_attributes, ft_low, ft_high;
	char *dirname, fullpath[PATH_MAX];
	DIR *pdir;
	struct dirent *pdirent;
	struct stat filestat;
	struct fileinfo *pfinfo;
	STREAM stmp;

	logger(Disk, Debug, "disk_query_directory(handle=0x%x, info_class=0x%x, pattern=%s, ...)",
	       handle, info_class, pattern);

	pfinfo = &(g_fileinfo[handle]);
	pdir = pfinfo->pdir;
	dirname = pfinfo->path;
	file_attributes = 0;

	switch (info_class)
	{
		case FileBothDirectoryInformation:
		case FileDirectoryInformation:
		case FileFullDirectoryInformation:
		case FileNamesInformation:

			/* If a search pattern is received, remember this pattern, and restart search */
			if (pattern != NULL && pattern[0] != 0)
			{
				strncpy(pfinfo->pattern, 1 + strrchr(pattern, '/'), PATH_MAX - 1);
				rewinddir(pdir);
			}

			/* find next dirent matching pattern */
			pdirent = readdir(pdir);
			while (pdirent && fnmatch(pfinfo->pattern, pdirent->d_name, 0) != 0)
				pdirent = readdir(pdir);

			if (pdirent == NULL)
				return RD_STATUS_NO_MORE_FILES;

			/* Get information for directory entry */
			sprintf(fullpath, "%s/%s", dirname, pdirent->d_name);

			if (stat(fullpath, &filestat))
			{
				switch (errno)
				{
					case ENOENT:
					case ELOOP:
					case EACCES:
						/* These are non-fatal errors. */
						memset(&filestat, 0, sizeof(filestat));
						break;
					default:
						/* Fatal error. By returning STATUS_NO_SUCH_FILE, 
						   the directory list operation will be aborted */
						logger(Disk, Error,
						       "disk_query_directory(), stat() failed: %s",
						       strerror(errno));
						out_uint8(out, 0);
						return RD_STATUS_NO_SUCH_FILE;
				}
			}

			if (S_ISDIR(filestat.st_mode))
				file_attributes |= FILE_ATTRIBUTE_DIRECTORY;
			if (pdirent->d_name[0] == '.')
				file_attributes |= FILE_ATTRIBUTE_HIDDEN;
			if (!file_attributes)
				file_attributes |= FILE_ATTRIBUTE_NORMAL;
			if (!(filestat.st_mode & S_IWUSR))
				file_attributes |= FILE_ATTRIBUTE_READONLY;

			/* Return requested information */
			out_uint32_le(out, 0);	/* NextEntryOffset */
			out_uint32_le(out, 0);	/* FileIndex zero */
			break;

		default:
			logger(Disk, Warning,
			       "disk_query_directory(), unhandled directory info class 0x%x",
			       info_class);
			return RD_STATUS_INVALID_PARAMETER;
	}

	// Write entry name as utf16 into stmp
	stmp = s_alloc(PATH_MAX * 4);
	out_utf16s_no_eos(stmp, pdirent->d_name);
	s_mark_end(stmp);

	switch (info_class)
	{
		case FileBothDirectoryInformation:

			seconds_since_1970_to_filetime(get_create_time(&filestat), &ft_high,
						       &ft_low);
			out_uint32_le(out, ft_low);	/* create time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_atime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* last_access_time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_mtime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* last_write_time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_ctime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* change_write_time */
			out_uint32_le(out, ft_high);

			out_uint64_le(out, filestat.st_size);	/* filesize */
			out_uint64_le(out, filestat.st_size);	/* filesize */
			out_uint32_le(out, file_attributes);	/* FileAttributes */
			out_uint32_le(out, s_length(stmp));	/* length of dir entry name string */
			out_uint32_le(out, 0);	/* EaSize */
			out_uint8(out, 0);	/* ShortNameLength */
			out_uint8s(out, 24);	/* ShortName (8.3 name) */
			out_stream(out, stmp);	/* dir entry name string */
			break;


		case FileDirectoryInformation:

			seconds_since_1970_to_filetime(get_create_time(&filestat), &ft_high,
						       &ft_low);
			out_uint32_le(out, ft_low);	/* create time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_atime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* last_access_time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_mtime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* last_write_time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_ctime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* change_write_time */
			out_uint32_le(out, ft_high);

			out_uint64_le(out, filestat.st_size);	/* filesize */
			out_uint64_le(out, filestat.st_size);	/* filesize */
			out_uint32_le(out, file_attributes);
			out_uint32_le(out, s_length(stmp));	/* dir entry name string length */
			out_stream(out, stmp);	/* dir entry name */
			break;


		case FileFullDirectoryInformation:

			seconds_since_1970_to_filetime(get_create_time(&filestat), &ft_high,
						       &ft_low);
			out_uint32_le(out, ft_low);	/* create time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_atime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* last_access_time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_mtime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* last_write_time */
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_ctime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	/* change_write_time */
			out_uint32_le(out, ft_high);

			out_uint64_le(out, filestat.st_size);	/* filesize */
			out_uint64_le(out, filestat.st_size);	/* filesize */
			out_uint32_le(out, file_attributes);
			out_uint32_le(out, s_length(stmp));	/* dir entry name string length */
			out_uint32_le(out, 0);	/* EaSize */
			out_stream(out, stmp);	/* dir entry name */
			break;


		case FileNamesInformation:

			out_uint32_le(out, s_length(stmp));	/* dir entry name string length */
			out_stream(out, stmp);	/* dir entry name */
			break;


		default:
			logger(Disk, Warning,
			       "disk_query_directory(), unhandled directory info class 0x%x",
			       info_class);
			s_free(stmp);
			return RD_STATUS_INVALID_PARAMETER;
	}

	s_free(stmp);

	return RD_STATUS_SUCCESS;
}



static RD_NTSTATUS
disk_device_control(RD_NTHANDLE handle, uint32 request, STREAM in, STREAM out)
{
	UNUSED(in);
	UNUSED(out);

	logger(Disk, Debug, "disk_device_control(handle=0x%x, request=0x%x, ...)", handle, request);
	if (((request >> 16) != 20) || ((request >> 16) != 9))
		return RD_STATUS_INVALID_PARAMETER;

	/* extract operation */
	request >>= 2;
	request &= 0xfff;

	switch (request)
	{
		case 25:	/* ? */
		case 42:	/* ? */
		default:
			logger(Disk, Warning, "disk_device_control(), unhandled disk ioctl %d",
			       request);
			return RD_STATUS_INVALID_PARAMETER;
	}

	return RD_STATUS_SUCCESS;
}

DEVICE_FNS disk_fns = {
	disk_create,
	disk_close,
	disk_read,
	disk_write,
	disk_device_control	/* device_control */
};
