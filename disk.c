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

#define FILE_ATTRIBUTE_READONLY			0x00000001
#define FILE_ATTRIBUTE_HIDDEN			0x00000002
#define FILE_ATTRIBUTE_SYSTEM			0x00000004
#define FILE_ATTRIBUTE_DIRECTORY		0x00000010
#define FILE_ATTRIBUTE_ARCHIVE			0x00000020
#define FILE_ATTRIBUTE_DEVICE			0x00000040
#define FILE_ATTRIBUTE_NORMAL			0x00000080
#define FILE_ATTRIBUTE_TEMPORARY		0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE		0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT		0x00000400
#define FILE_ATTRIBUTE_COMPRESSED		0x00000800
#define FILE_ATTRIBUTE_OFFLINE			0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED	0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED		0x00004000

#define FILE_BASIC_INFORMATION			0x04
#define FILE_STANDARD_INFORMATION		0x05

#define FS_CASE_SENSITIVE			0x00000001
#define FS_CASE_IS_PRESERVED			0x00000002
#define FS_UNICODE_STORED_ON_DISK		0x00000004
#define FS_PERSISTENT_ACLS			0x00000008
#define FS_FILE_COMPRESSION			0x00000010
#define FS_VOLUME_QUOTAS			0x00000020
#define FS_SUPPORTS_SPARSE_FILES		0x00000040
#define FS_SUPPORTS_REPARSE_POINTS		0x00000080
#define FS_SUPPORTS_REMOTE_STORAGE		0X00000100
#define FS_VOL_IS_COMPRESSED			0x00008000
#define FILE_READ_ONLY_VOLUME			0x00080000

#define OPEN_EXISTING				1
#define CREATE_NEW				2
#define OPEN_ALWAYS				3
#define TRUNCATE_EXISTING			4
#define CREATE_ALWAYS				5

#define GENERIC_READ				0x80000000
#define GENERIC_WRITE				0x40000000
#define GENERIC_EXECUTE				0x20000000
#define GENERIC_ALL				0x10000000

#define ERROR_FILE_NOT_FOUND			2L
#define ERROR_ALREADY_EXISTS			183L

#define	MAX_OPEN_FILES	0x100

#if (defined(sun) && (defined(__svr4__) || defined(__SVR4)))
#define SOLARIS
#endif

#ifdef SOLARIS
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

#if defined(SOLARIS)
#include <sys/statvfs.h>	/* solaris statvfs */
#define STATFS_FN(path, buf) (statvfs(path,buf))
#define STATFS_T statvfs
#define F_NAMELEN(buf) ((buf).f_namemax)

#elif defined(__OpenBSD__)
#include <sys/param.h>
#include <sys/mount.h>
#define STATFS_FN(path, buf) (statfs(path,buf))
#define STATFS_T statfs
#define F_NAMELEN(buf) (NAME_MAX)

#else
#include <sys/vfs.h>		/* linux statfs */
#define STATFS_FN(path, buf) (statfs(path,buf))
#define STATFS_T statfs
#define F_NAMELEN(buf) ((buf).f_namelen)
#endif

#include "rdesktop.h"

extern RDPDR_DEVICE g_rdpdr_device[];

struct fileinfo
{
	uint32 device_id, flags_and_attributes;
	char path[256];
	DIR *pdir;
	struct dirent *pdirent;
	char pattern[64];
	BOOL delete_on_close;
}
g_fileinfo[MAX_OPEN_FILES];

/* Convert seconds since 1970 to a filetime */
void
seconds_since_1970_to_filetime(time_t seconds, uint32 * high, uint32 * low)
{
	unsigned long long ticks;

	ticks = (seconds + 11644473600LL) * 10000000;
	*low = (uint32) ticks;
	*high = (uint32) (ticks >> 32);
}

/* Enumeration of devices from rdesktop.c        */
/* returns numer of units found and initialized. */
/* optarg looks like ':h:=/mnt/floppy,b:=/mnt/usbdevice1' */
/* when it arrives to this function.             */
int
disk_enum_devices(int *id, char *optarg)
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

/* Opens of creates a file or directory */
NTSTATUS
disk_create(uint32 device_id, uint32 accessmask, uint32 sharemode, uint32 create_disposition,
	    uint32 flags_and_attributes, char *filename, HANDLE * phandle)
{
	HANDLE handle;
	DIR *dirp;
	int flags, mode;
	char path[256];

	handle = 0;
	dirp = NULL;
	flags = 0;
	mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	if (filename[strlen(filename) - 1] == '/')
		filename[strlen(filename) - 1] = 0;
	sprintf(path, "%s%s", g_rdpdr_device[device_id].local_path, filename);
	//printf("Open: %s\n", path);

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
				case EACCES:

					return STATUS_ACCESS_DENIED;

				case ENOENT:

					return STATUS_NO_SUCH_FILE;

				default:

					perror("open");
					return STATUS_NO_SUCH_FILE;
			}
		}
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

	if (offset)
		lseek(handle, offset, SEEK_SET);
	n = read(handle, data, length);

	if (n < 0)
	{
		perror("read");
		*result = 0;
		return STATUS_INVALID_PARAMETER;
	}

	*result = n;

	return STATUS_SUCCESS;
}

NTSTATUS
disk_write(HANDLE handle, uint8 * data, uint32 length, uint32 offset, uint32 * result)
{
	int n;

	if (offset)
		lseek(handle, offset, SEEK_SET);

	n = write(handle, data, length);

	if (n < 0)
	{
		perror("write");
		*result = 0;
		return STATUS_ACCESS_DENIED;
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
	{
		file_attributes |= FILE_ATTRIBUTE_DIRECTORY;
	}
	filename = 1 + strrchr(path, '/');
	if (filename && filename[0] == '.')
	{
		file_attributes |= FILE_ATTRIBUTE_HIDDEN;
	}

	// Return requested data
	switch (info_class)
	{
		case 4:	/* FileBasicInformation */

			out_uint8s(out, 8);	//create_time not available;

			seconds_since_1970_to_filetime(filestat.st_atime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//last_access_time
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(filestat.st_mtime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//last_write_time
			out_uint32_le(out, ft_high);

			out_uint8s(out, 8);	//unknown zero
			out_uint32_le(out, file_attributes);
			break;

		case 5:	/* FileStandardInformation */

			out_uint32_le(out, filestat.st_size);	//Allocation size
			out_uint32_le(out, 0);
			out_uint32_le(out, filestat.st_size);	//End of file
			out_uint32_le(out, 0);
			out_uint32_le(out, filestat.st_nlink);	//Number of links
			out_uint8(out, 0);	//Delete pending
			out_uint8(out, S_ISDIR(filestat.st_mode) ? 1 : 0);	//Directory
			break;

		case 35:	/* FileObjectIdInformation */

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

	pfinfo = &(g_fileinfo[handle]);

	switch (info_class)
	{
		case 4:	/* FileBasicInformation */

			// Probably safe to ignore
			break;

		case 10:	/* FileRenameInformation */

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

		case 13:	/* FileDispositionInformation */

			//unimpl("IRP Set File Information class: FileDispositionInformation\n");
			// in_uint32_le(in, delete_on_close);
			// disk_close(handle);
			unlink(pfinfo->path);
			break;

		case 19:	/* FileAllocationInformation */

			unimpl("IRP Set File Information class: FileAllocationInformation\n");
			break;

		case 20:	/* FileEndOfFileInformation */

			unimpl("IRP Set File Information class: FileEndOfFileInformation\n");
			break;

		default:

			unimpl("IRP Set File Information class: 0x%x\n", info_class);
			return STATUS_INVALID_PARAMETER;
	}
	return STATUS_SUCCESS;
}

NTSTATUS
disk_query_volume_information(HANDLE handle, uint32 info_class, STREAM out)
{
	char *volume, *fs_type;
	struct STATFS_T stat_fs;
	struct fileinfo *pfinfo;

	pfinfo = &(g_fileinfo[handle]);
	volume = "RDESKTOP";
	fs_type = "RDPFS";

	if (STATFS_FN(pfinfo->path, &stat_fs) != 0)	/* FIXME: statfs is not portable */
	{
		perror("statfs");
		return STATUS_ACCESS_DENIED;
	}

	switch (info_class)
	{
		case 1:	/* FileFsVolumeInformation */

			out_uint32_le(out, 0);	/* volume creation time low */
			out_uint32_le(out, 0);	/* volume creation time high */
			out_uint32_le(out, 0);	/* serial */
			out_uint32_le(out, 2 * strlen(volume));	/* length of string */
			out_uint8(out, 0);	/* support objects? */
			rdp_out_unistr(out, volume, 2 * strlen(volume) - 2);
			break;

		case 3:	/* FileFsSizeInformation */

			out_uint32_le(out, stat_fs.f_blocks);	/* Total allocation units low */
			out_uint32_le(out, 0);	/* Total allocation high units */
			out_uint32_le(out, stat_fs.f_bfree);	/* Available allocation units */
			out_uint32_le(out, 0);	/* Available allowcation units */
			out_uint32_le(out, stat_fs.f_bsize / 0x200);	/* Sectors per allocation unit */
			out_uint32_le(out, 0x200);	/* Bytes per sector */
			break;

		case 5:	/* FileFsAttributeInformation */

			out_uint32_le(out, FS_CASE_SENSITIVE | FS_CASE_IS_PRESERVED);	/* fs attributes */
			out_uint32_le(out, F_NAMELEN(stat_fs));	/* max length of filename */
			out_uint32_le(out, 2 * strlen(fs_type));	/* length of fs_type */
			rdp_out_unistr(out, fs_type, 2 * strlen(fs_type) - 2);
			break;

		case 2:	/* FileFsLabelInformation */
		case 4:	/* FileFsDeviceInformation */
		case 6:	/* FileFsControlInformation */
		case 7:	/* FileFsFullSizeInformation */
		case 8:	/* FileFsObjectIdInformation */
		case 9:	/* FileFsMaximumInformation */
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
			{
				pdirent = readdir(pdir);
			}

			if (pdirent == NULL)
			{
				return STATUS_NO_MORE_FILES;
			}

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

			// Return requested information
			out_uint8s(out, 8);	//unknown zero
			out_uint8s(out, 8);	//create_time not available in posix;

			seconds_since_1970_to_filetime(fstat.st_atime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//last_access_time
			out_uint32_le(out, ft_high);

			seconds_since_1970_to_filetime(fstat.st_mtime, &ft_high, &ft_low);
			out_uint32_le(out, ft_low);	//last_write_time
			out_uint32_le(out, ft_high);

			out_uint8s(out, 8);	//unknown zero
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

DEVICE_FNS disk_fns = {
	disk_create,
	disk_close,
	disk_read,
	disk_write,
	NULL			/* device_control */
};
