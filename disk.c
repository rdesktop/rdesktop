/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Disk Redirection
   Copyright (C) Jeroen Meijer 2003

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "disk.h"

#if (defined(sun) && (defined(__svr4__) || defined(__SVR4)))
#define SOLARIS
#endif

#if (defined(SOLARIS) || defined(__hpux))
#define DIRFD(a) ((a)->dd_fd)
#else
#define DIRFD(a) (dirfd(a))
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>		/* open, close */
#include <dirent.h>		/* opendir, closedir, readdir */
#include <fnmatch.h>
#include <errno.h>		/* errno */

#include <utime.h>
#include <time.h>		/* ctime */


#if (defined(SOLARIS) || defined (__hpux) || defined(__BEOS__))
#include <sys/statvfs.h>	/* solaris statvfs */
#include <sys/mntent.h>
/* TODO: Fix mntent-handling for solaris */
#undef HAVE_MNTENT_H
#define MNTENT_PATH "/etc/mnttab"
#define STATFS_FN(path, buf) (statvfs(path,buf))
#define STATFS_T statvfs
#define F_NAMELEN(buf) ((buf).f_namemax)

#elif (defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__))
#include <sys/param.h>
#include <sys/mount.h>
#define STATFS_FN(path, buf) (statfs(path,buf))
#define STATFS_T statfs
#define F_NAMELEN(buf) (NAME_MAX)

#else
#include <sys/vfs.h>		/* linux statfs */
#include <mntent.h>
#define HAVE_MNTENT_H
#define MNTENT_PATH "/etc/mtab"
#define STATFS_FN(path, buf) (statfs(path,buf))
#define STATFS_T statfs
#define F_NAMELEN(buf) ((buf).f_namelen)
#endif

#include "rdesktop.h"

extern RDPDR_DEVICE g_rdpdr_device[];

FILEINFO g_fileinfo[MAX_OPEN_FILES];

typedef struct
{
	char name[256];
	char label[256];
	unsigned long serial;
	char type[256];
} FsInfoType;


time_t
get_create_time(struct stat *st)
{
	time_t ret, ret1;

	ret = MIN(st->st_ctime, st->st_mtime);
	ret1 = MIN(ret, st->st_atime);

	if (ret1 != (time_t) 0)
		return ret1;

	return ret;
}

/* Convert seconds since 1970 to a filetime */
void
seconds_since_1970_to_filetime(time_t seconds, uint32 * high, uint32 * low)
{
	unsigned long long ticks;

	ticks = (seconds + 11644473600LL) * 10000000;
	*low = (uint32) ticks;
	*high = (uint32) (ticks >> 32);
}

/* Convert seconds since 1970 back to filetime */
time_t
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


/* Enumeration of devices from rdesktop.c        */
/* returns numer of units found and initialized. */
/* optarg looks like ':h=/mnt/floppy,b=/mnt/usbdevice1' */
/* when it arrives to this function.             */
int
disk_enum_devices(uint32 * id, char *optarg)
{
	char *pos = optarg;
	char *pos2;
	int count = 0;

	// skip the first colon
	optarg++;
	while ((pos = next_arg(optarg, ',')) && *id < RDPDR_MAX_DEVICES)
	{
		pos2 = next_arg(optarg, '=');
		strcpy(g_rdpdr_device[*id].name, optarg);

		toupper_str(g_rdpdr_device[*id].name);

		/* add trailing colon to name. */
		strcat(g_rdpdr_device[*id].name, ":");

		g_rdpdr_device[*id].local_path = xmalloc(strlen(pos2) + 1);
		strcpy(g_rdpdr_device[*id].local_path, pos2);
		printf("DISK %s to %s\n", g_rdpdr_device[*id].name, g_rdpdr_device[*id].local_path);
		g_rdpdr_device[*id].device_type = DEVICE_TYPE_DISK;
		count++;
		(*id)++;

		optarg = pos;
	}
	return count;
}

/* Opens or creates a file or directory */
NTSTATUS
disk_create(uint32 device_id, uint32 accessmask, uint32 sharemode, uint32 create_disposition,
	    uint32 flags_and_attributes, char *filename, HANDLE * phandle)
{
	HANDLE handle;
	DIR *dirp;
	int flags, mode;
	char path[256];
	struct stat filestat;

	handle = 0;
	dirp = NULL;
	flags = 0;
	mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;


	if (filename[strlen(filename) - 1] == '/')
		filename[strlen(filename) - 1] = 0;
	sprintf(path, "%s%s", g_rdpdr_device[device_id].local_path, filename);

	switch (create_disposition)
	{
		case CREATE_ALWAYS:

			// Delete existing file/link.
			unlink(path);
			flags |= O_CREAT;
			break;

		case CREATE_NEW:

			// If the file already exists, then fail.
			flags |= O_CREAT | O_EXCL;
			break;

		case OPEN_ALWAYS:

			// Create if not already exists.
			flags |= O_CREAT;
			break;

		case OPEN_EXISTING:

			// Default behaviour
			break;

		case TRUNCATE_EXISTING:

			// If the file does not exist, then fail.
			flags |= O_TRUNC;
			break;
	}

	//printf("Open: \"%s\"  flags: %u, accessmask: %u sharemode: %u create disp: %u\n", path, flags_and_attributes, accessmask, sharemode, create_disposition);

	// Get information about file and set that flag ourselfs
	if ((stat(path, &filestat) == 0) && (S_ISDIR(filestat.st_mode)))
	{
		if (flags_and_attributes & FILE_NON_DIRECTORY_FILE)
			return STATUS_FILE_IS_A_DIRECTORY;
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

					return STATUS_ACCESS_DENIED;

				case ENOENT:

					return STATUS_NO_SUCH_FILE;

				default:

					perror("opendir");
					return STATUS_NO_SUCH_FILE;
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

		handle = open(path, flags, mode);
		if (handle == -1)
		{
			switch (errno)
			{
				case EISDIR:

					return STATUS_FILE_IS_A_DIRECTORY;

				case EACCES:

					return STATUS_ACCESS_DENIED;

				case ENOENT:

					return STATUS_NO_SUCH_FILE;
				case EEXIST:

					return STATUS_OBJECT_NAME_COLLISION;
				default:

					perror("open");
					return STATUS_NO_SUCH_FILE;
			}
		}

		/* all read and writes of files should be non blocking */
		if (fcntl(handle, F_SETFL, O_NONBLOCK) == -1)
			perror("fcntl");
	}

	if (handle >= MAX_OPEN_FILES)
	{
		error("Maximum number of open files (%s) reached. Increase MAX_OPEN_FILES!\n",
		      handle);
		exit(1);
	}

	if (dirp)
		g_fileinfo[handle].pdir = dirp;
	g_fileinfo[handle].device_id = device_id;
	g_fileinfo[handle].flags_and_attributes = flags_and_attributes;
	strncpy(g_fileinfo[handle].path, path, 255);

	*phandle = handle;
	return STATUS_SUCCESS;
}

NTSTATUS
disk_close(HANDLE handle)
{
	struct fileinfo *pfinfo;

	pfinfo = &(g_fileinfo[handle]);

	if (pfinfo->flags_and_attributes & FILE_DIRECTORY_FILE)
	{
		closedir(pfinfo->pdir);
		//FIXME: Should check exit code
	}
	else
	{
		close(handle);
	}

	return STATUS_SUCCESS;
}

NTSTATUS
disk_read(HANDLE handle, uint8 * data, uint32 length, uint32 offset, uint32 * result)
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
				return STATUS_FILE_IS_A_DIRECTORY;
			default:
				perror("read");
				return STATUS_INVALID_PARAMETER;
		}
	}

	*result = n;

	return STATUS_SUCCESS;
}

NTSTATUS
disk_write(HANDLE handle, uint8 * data, uint32 length, uint32 offset, uint32 * result)
{
	int n;

	lseek(handle, offset, SEEK_SET);

	n = write(handle, data, length);

	if (n < 0)
	{
		perror("write");
		*result = 0;
		switch (errno)
		{
			case ENOSPC:
				return STATUS_DISK_FULL;
			default:
				return STATUS_ACCESS_DENIED;
		}
	}

	*result = n;

	return STATUS_SUCCESS;
}

NTSTATUS
disk_query_information(HANDLE handle, uint32 info_class, STREAM out)
{
	uint32 file_attributes, ft_high, ft_low;
	struct stat filestat;
	char *path, *filename;

	path = g_fileinfo[handle].path;

	// Get information about file
	if (fstat(handle, &filestat) != 0)
	{
		perror("stat");
		out_uint8(out, 0);
		return STATUS_ACCESS_DENIED;
	}

	// Set file attributes
	file_attributes = 0;
	if (S_ISDIR(filestat.st_mode))
		file_attributes |= FILE_ATTRIBUTE_DIRECTORY;

	filename = 1 + strrchr(path, '/');
	if (filename && filename[0] == '.')
		file_attributes |= FILE_ATTRIBUTE_HIDDEN;

	if (!file_attributes)
		file_attributes |= FILE_ATTRIBUTE_NORMAL;

	if (!(filestat.st_mode & S_IWUSR))
		file_attributes |= FILE_ATTRIBUTE_READONLY;

	// Return requested data
	switch (info_class)
	{
		case FileBasicInformation:
			seconds_since_1970_to_filetime(get_create_time(&filestat), &ft_high,
						       &ft_low);
			out_uint32_le(out, ft_low);	//create_access_time
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_atime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//last_access_time
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_mtime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//last_write_time
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_ctime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//last_change_time
			out_uint32_le(out, ft_high);

			out_uint32_le(out, file_attributes);
			break;

		case FileStandardInformation:

			out_uint32_le(out, filestat.st_size);	//Allocation size
			out_uint32_le(out, 0);
			out_uint32_le(out, filestat.st_size);	//End of file
			out_uint32_le(out, 0);
			out_uint32_le(out, filestat.st_nlink);	//Number of links
			out_uint8(out, 0);	//Delete pending
			out_uint8(out, S_ISDIR(filestat.st_mode) ? 1 : 0);	//Directory
			break;

		case FileObjectIdInformation:

			out_uint32_le(out, file_attributes);	/* File Attributes */
			out_uint32_le(out, 0);	/* Reparse Tag */
			break;

		default:

			unimpl("IRP Query (File) Information class: 0x%x\n", info_class);
			return STATUS_INVALID_PARAMETER;
	}
	return STATUS_SUCCESS;
}

NTSTATUS
disk_set_information(HANDLE handle, uint32 info_class, STREAM in, STREAM out)
{
	uint32 device_id, length, file_attributes, ft_high, ft_low;
	char newname[256], fullpath[256];
	struct fileinfo *pfinfo;

	int mode;
	struct stat filestat;
	time_t write_time, change_time, access_time, mod_time;
	struct utimbuf tvs;
	struct STATFS_T stat_fs;

	pfinfo = &(g_fileinfo[handle]);

	switch (info_class)
	{
		case FileBasicInformation:
			write_time = change_time = access_time = 0;

			in_uint8s(in, 4);	/* Handle of root dir? */
			in_uint8s(in, 24);	/* unknown */

			// CreationTime
			in_uint32_le(in, ft_low);
			in_uint32_le(in, ft_high);

			// AccessTime
			in_uint32_le(in, ft_low);
			in_uint32_le(in, ft_high);
			if (ft_low || ft_high)
				access_time = convert_1970_to_filetime(ft_high, ft_low);

			// WriteTime
			in_uint32_le(in, ft_low);
			in_uint32_le(in, ft_high);
			if (ft_low || ft_high)
				write_time = convert_1970_to_filetime(ft_high, ft_low);

			// ChangeTime
			in_uint32_le(in, ft_low);
			in_uint32_le(in, ft_high);
			if (ft_low || ft_high)
				change_time = convert_1970_to_filetime(ft_high, ft_low);

			in_uint32_le(in, file_attributes);

			if (fstat(handle, &filestat))
				return STATUS_ACCESS_DENIED;

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
#if WITH_DEBUG_RDP5
				printf("FileBasicInformation access       time %s",
				       ctime(&tvs.actime));
				printf("FileBasicInformation modification time %s",
				       ctime(&tvs.modtime));
#endif
				if (utime(pfinfo->path, &tvs))
					return STATUS_ACCESS_DENIED;
			}

			if (!file_attributes)
				break;	// not valid

			mode = filestat.st_mode;

			if (file_attributes & FILE_ATTRIBUTE_READONLY)
				mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
			else
				mode |= S_IWUSR;

			mode &= 0777;
#if WITH_DEBUG_RDP5
			printf("FileBasicInformation set access mode 0%o", mode);
#endif

			if (fchmod(handle, mode))
				return STATUS_ACCESS_DENIED;

			break;

		case FileRenameInformation:

			in_uint8s(in, 4);	/* Handle of root dir? */
			in_uint8s(in, 0x1a);	/* unknown */
			in_uint32_le(in, length);

			if (length && (length / 2) < 256)
			{
				rdp_in_unistr(in, newname, length);
				convert_to_unix_filename(newname);
			}
			else
			{
				return STATUS_INVALID_PARAMETER;
			}

			sprintf(fullpath, "%s%s", g_rdpdr_device[pfinfo->device_id].local_path,
				newname);

			if (rename(pfinfo->path, fullpath) != 0)
			{
				perror("rename");
				return STATUS_ACCESS_DENIED;
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
			   http://www.osronline.com/article.cfm?article=245. Currently,
			   we are deleting the file immediately. I
			   guess this is a FIXME. */

			//in_uint32_le(in, delete_on_close);

			/* Make sure we close the file before
			   unlinking it. Not doing so would trigger
			   silly-delete if using NFS, which might fail
			   on FAT floppies, for example. */
			disk_close(handle);

			if ((pfinfo->flags_and_attributes & FILE_DIRECTORY_FILE))	// remove a directory
			{
				if (rmdir(pfinfo->path) < 0)
					return STATUS_ACCESS_DENIED;
			}
			else if (unlink(pfinfo->path) < 0)	// unlink a file
				return STATUS_ACCESS_DENIED;

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
			if (STATFS_FN(g_rdpdr_device[pfinfo->device_id].local_path, &stat_fs) == 0)
				if (stat_fs.f_bsize * stat_fs.f_bfree < length)
					return STATUS_DISK_FULL;

			if (ftruncate(handle, length) != 0)
			{
				perror("ftruncate");
				return STATUS_DISK_FULL;
			}

			break;
		default:

			unimpl("IRP Set File Information class: 0x%x\n", info_class);
			return STATUS_INVALID_PARAMETER;
	}
	return STATUS_SUCCESS;
}

FsInfoType *
FsVolumeInfo(char *fpath)
{

#ifdef HAVE_MNTENT_H
	FILE *fdfs;
	struct mntent *e;
	static FsInfoType info;

	/* initialize */
	memset(&info, 0, sizeof(info));
	strcpy(info.label, "RDESKTOP");
	strcpy(info.type, "RDPFS");

	fdfs = setmntent(MNTENT_PATH, "r");
	if (!fdfs)
		return &info;

	while ((e = getmntent(fdfs)))
	{
		if (strncmp(fpath, e->mnt_dir, strlen(fpath)) == 0)
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
						strncpy(info.label, buf + 43, 10);
						info.label[10] = '\0';
					}
					else if (lseek(fd, 32767, SEEK_SET) >= 0)	/* ISO9660 */
					{
						read(fd, buf, sizeof(buf));
						strncpy(info.label, buf + 41, 32);
						info.label[32] = '\0';
						//info.Serial = (buf[128]<<24)+(buf[127]<<16)+(buf[126]<<8)+buf[125];
					}
					close(fd);
				}
			}
		}
	}
	endmntent(fdfs);
#else
	static FsInfoType info;

	/* initialize */
	memset(&info, 0, sizeof(info));
	strcpy(info.label, "RDESKTOP");
	strcpy(info.type, "RDPFS");

#endif
	return &info;
}


NTSTATUS
disk_query_volume_information(HANDLE handle, uint32 info_class, STREAM out)
{
	struct STATFS_T stat_fs;
	struct fileinfo *pfinfo;
	FsInfoType *fsinfo;

	pfinfo = &(g_fileinfo[handle]);

	if (STATFS_FN(pfinfo->path, &stat_fs) != 0)
	{
		perror("statfs");
		return STATUS_ACCESS_DENIED;
	}

	fsinfo = FsVolumeInfo(pfinfo->path);

	switch (info_class)
	{
		case FileFsVolumeInformation:

			out_uint32_le(out, 0);	/* volume creation time low */
			out_uint32_le(out, 0);	/* volume creation time high */
			out_uint32_le(out, fsinfo->serial);	/* serial */

			out_uint32_le(out, 2 * strlen(fsinfo->label));	/* length of string */

			out_uint8(out, 0);	/* support objects? */
			rdp_out_unistr(out, fsinfo->label, 2 * strlen(fsinfo->label) - 2);
			break;

		case FileFsSizeInformation:

			out_uint32_le(out, stat_fs.f_blocks);	/* Total allocation units low */
			out_uint32_le(out, 0);	/* Total allocation high units */
			out_uint32_le(out, stat_fs.f_bfree);	/* Available allocation units */
			out_uint32_le(out, 0);	/* Available allowcation units */
			out_uint32_le(out, stat_fs.f_bsize / 0x200);	/* Sectors per allocation unit */
			out_uint32_le(out, 0x200);	/* Bytes per sector */
			break;

		case FileFsAttributeInformation:

			out_uint32_le(out, FS_CASE_SENSITIVE | FS_CASE_IS_PRESERVED);	/* fs attributes */
			out_uint32_le(out, F_NAMELEN(stat_fs));	/* max length of filename */

			out_uint32_le(out, 2 * strlen(fsinfo->type));	/* length of fs_type */
			rdp_out_unistr(out, fsinfo->type, 2 * strlen(fsinfo->type) - 2);
			break;

		case FileFsLabelInformation:
		case FileFsDeviceInformation:
		case FileFsControlInformation:
		case FileFsFullSizeInformation:
		case FileFsObjectIdInformation:
		case FileFsMaximumInformation:

		default:

			unimpl("IRP Query Volume Information class: 0x%x\n", info_class);
			return STATUS_INVALID_PARAMETER;
	}
	return STATUS_SUCCESS;
}

NTSTATUS
disk_query_directory(HANDLE handle, uint32 info_class, char *pattern, STREAM out)
{
	uint32 file_attributes, ft_low, ft_high;
	char *dirname, fullpath[256];
	DIR *pdir;
	struct dirent *pdirent;
	struct stat fstat;
	struct fileinfo *pfinfo;

	pfinfo = &(g_fileinfo[handle]);
	pdir = pfinfo->pdir;
	dirname = pfinfo->path;
	file_attributes = 0;

	switch (info_class)
	{
		case 3:	//FIXME: Why 3?

			// If a search pattern is received, remember this pattern, and restart search
			if (pattern[0] != 0)
			{
				strncpy(pfinfo->pattern, 1 + strrchr(pattern, '/'), 64);
				rewinddir(pdir);
			}

			// find next dirent matching pattern
			pdirent = readdir(pdir);
			while (pdirent && fnmatch(pfinfo->pattern, pdirent->d_name, 0) != 0)
				pdirent = readdir(pdir);

			if (pdirent == NULL)
				return STATUS_NO_MORE_FILES;

			// Get information for directory entry
			sprintf(fullpath, "%s/%s", dirname, pdirent->d_name);

			/* JIF
			   printf("Stat: %s\n", fullpath); */
			if (stat(fullpath, &fstat))
			{
				perror("stat");
				out_uint8(out, 0);
				return STATUS_ACCESS_DENIED;
			}

			if (S_ISDIR(fstat.st_mode))
				file_attributes |= FILE_ATTRIBUTE_DIRECTORY;
			if (pdirent->d_name[0] == '.')
				file_attributes |= FILE_ATTRIBUTE_HIDDEN;
			if (!file_attributes)
				file_attributes |= FILE_ATTRIBUTE_NORMAL;
			if (!(fstat.st_mode & S_IWUSR))
				file_attributes |= FILE_ATTRIBUTE_READONLY;

			// Return requested information
			out_uint8s(out, 8);	//unknown zero

			seconds_since_1970_to_filetime(get_create_time(&fstat), &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	// create time
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(fstat.st_atime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//last_access_time
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(fstat.st_mtime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//last_write_time
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(fstat.st_ctime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//change_write_time
			out_uint32_le(out, ft_high);

			out_uint32_le(out, fstat.st_size);	//filesize low
			out_uint32_le(out, 0);	//filesize high
			out_uint32_le(out, fstat.st_size);	//filesize low
			out_uint32_le(out, 0);	//filesize high
			out_uint32_le(out, file_attributes);
			out_uint8(out, 2 * strlen(pdirent->d_name) + 2);	//unicode length
			out_uint8s(out, 7);	//pad?
			out_uint8(out, 0);	//8.3 file length
			out_uint8s(out, 2 * 12);	//8.3 unicode length
			rdp_out_unistr(out, pdirent->d_name, 2 * strlen(pdirent->d_name));
			break;

		default:

			unimpl("IRP Query Directory sub: 0x%x\n", info_class);
			return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}



static NTSTATUS
disk_device_control(HANDLE handle, uint32 request, STREAM in, STREAM out)
{
	uint32 result;

	if (((request >> 16) != 20) || ((request >> 16) != 9))
		return STATUS_INVALID_PARAMETER;

	/* extract operation */
	request >>= 2;
	request &= 0xfff;

	printf("DISK IOCTL %d\n", request);

	switch (request)
	{
		case 25:	// ?
		case 42:	// ?
		default:
			unimpl("DISK IOCTL %d\n", request);
			return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}

DEVICE_FNS disk_fns = {
	disk_create,
	disk_close,
	disk_read,
	disk_write,
	disk_device_control	/* device_control */
};
