#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <3ds.h>
#include <sys/stat.h>

Result init()
{
	Result rc;
	
	rc = ptmSysmInit();
	if (R_FAILED(rc))
	{
		printf("ptmSysmInit: %08lX\n", rc);
		return rc;
	}
	printf("PTM Init Successful!\n");
	
	rc = amInit();
	if (R_FAILED(rc))
	{
		printf("amInit: %08lX\n", rc);
		return rc;
	}
	printf("AM Init Successful!\n");
	
	rc = fsInit();
	if (R_FAILED(rc))
	{
		printf("fsInit: %08lX\n", rc);
		return rc;
	}
	printf("FS Init Successful!\n");
	
	return 0;
}

void deInit()
{
	ptmSysmExit();
	amExit();
	fsExit();
	gfxExit();
}

void reboot(bool showPrompt)
{
	if (showPrompt)
	{
		printf("\n\nPress START to exit.\n");
		
		while (aptMainLoop())
		{
			gspWaitForVBlank();
			hidScanInput();

			u32 kDown = hidKeysDown();
			if (kDown & KEY_START)
				break;
		}
	}
	
	PTMSYSM_RebootAsync(0);
	deInit();
}

void recursiveDelDir(const char* path)
{
	if ((unlink(path) != 0) && (rmdir(path) != 0))
	{
		char path2[300];
		struct dirent* de;
		DIR* dir;
	
		if (!(dir = opendir(path)))
		{ 
			printf("could not open dir in recursiveDelDir\npath: %s\n", path);
			return;
		} 
	  
		while ((de = readdir(dir)) != NULL)
		{
			sprintf(path2, "%s/%s", path, de->d_name);
			recursiveDelDir(path2);
		}
		
		closedir(dir);
		
		if (rmdir(path) != 0)
		{
			printf("deleted all items in a non-empty dir, but rmdir still failed\npath: %s\n", path);
		}
	}
}

// Modeled from https://github.com/joel16/3DShell/blob/master/source/cia.c
Result installSdCia(const char* path)
{
	Result rc = 0;
	u32 bytes_read = 0, bytes_written = 0;
	u64 size = 0, offset = 0;
	Handle dst_handle, src_handle;
	AM_TitleEntry title;
	FS_Path ciaPath = fsMakePath(PATH_ASCII, path);

	if (R_FAILED(rc = FSUSER_OpenFileDirectly(&src_handle, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), ciaPath, FS_OPEN_READ, 0))) {
		printf("FSUSER_OpenFileDirectly: %08lX\nPath: %s\n", rc, path);
		return rc;
	}
	
	if (R_FAILED(rc = AM_GetCiaFileInfo(MEDIATYPE_SD, &title, src_handle))) {
		printf("AM_GetCiaFileInfo: %08lX\n", rc);
		return rc;
	}
	
	if (R_FAILED(rc = AM_DeleteAppTitle(MEDIATYPE_SD, title.titleID))) { 
		printf("AM_DeleteAppTitle: %08lX\n", rc); // Failing here is fine, as the app will not already be installed in most cases
	}
	
	if (R_FAILED(rc = FSFILE_GetSize(src_handle, &size))) {
		printf("FSFILE_GetSize: %08lX\n", rc);
		return rc;
	}
	
	if (R_FAILED(rc = AM_StartCiaInstall(MEDIATYPE_SD, &dst_handle))) {
		printf("AM_StartCiaInstall: %08lX\n", rc);
		return rc;
	}

	size_t buf_size = 0x10000;
	u8 *buf = malloc(buf_size);
	printf("Installing 000%13llX ...\n", title.titleID);
	
	do {
		memset(buf, 0, buf_size);

		if (R_FAILED(rc = FSFILE_Read(src_handle, &bytes_read, offset, buf, buf_size))) {
			free(buf);
			FSFILE_Close(src_handle);
			FSFILE_Close(dst_handle);
			printf("FSFILE_Read: %08lX\n", rc);
			return rc;
		}
		if (R_FAILED(rc = FSFILE_Write(dst_handle, &bytes_written, offset, buf, bytes_read, FS_WRITE_FLUSH))) {
			free(buf);
			FSFILE_Close(src_handle);
			FSFILE_Close(dst_handle);
			printf("FSFILE_Write: %08lX\n", rc);
			return rc;
		}

		offset += bytes_read;
	} while(offset < size);

	if (bytes_read != bytes_written) {
		AM_CancelCIAInstall(dst_handle);
		free(buf);
		printf("CIA bytes written mismatch: %08lX\n", rc);
		return rc;
	}
	
	free(buf);

	if (R_FAILED(rc = AM_FinishCiaInstall(dst_handle))) {
		printf("AM_FinishCiaInstall: %08lX", rc);
		return rc;
	}

	if (R_FAILED(rc = FSFILE_Close(src_handle))) {
		printf("FSFILE_Close: %08lX", rc);
		return rc;
	}

	return 0;
}

int main()
{
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);
	
	//Result rc;
	FILE* file;
	DIR* dir;
	char path[300], path2[300];
	char id0[33] = { '\0' };
	char id1[33] = { '\0' };
	struct dirent* de;
	
	if(R_FAILED(init()))
	{
		printf("Init failed!\n");
		reboot(true);
		return 1;
	}
	
	remove("sdmc:/luma/config.bin");
	remove("sdmc:/boot.3dsx");
	rename("sdmc:/cartInstallWorkDir/config.bin.bak", "sdmc:/luma/config.bin");
	rename("sdmc:/cartInstallWorkDir/boot.3dsx.bak", "sdmc:/boot.3dsx");
	
	if (!(file = fopen("sdmc:/cartInstallWorkDir/id0.txt", "r")))
	{
		printf("Could not open id0.txt\n");
		reboot(true);
		return 1;
	}
	
	if (fread(id0, 1, 32, file) != 32)
	{
		printf("Could not read id0.txt\n");
		fclose(file);
		reboot(true);
		return 1;
	}
	
	fclose(file);
	
	sprintf(path, "sdmc:/Nintendo 3DS/%s", id0);
	printf("id0: %s\n", id0);
	if (!(dir = opendir(path)))
    { 
        printf("Could not open id0 dir\nattempted path: %s\n", path); 
        reboot(true);
		return 1;
    }
	
	int c = 0;
	while ((de = readdir(dir)) != NULL)
	{
		strcpy(id1, de->d_name);
		c++;
	}
	
	closedir(dir);
	
	if (c > 1)
	{
		printf("multiple id1 dirs. handling for this isn't implemented yet. go yell at asp.\n");
		reboot(true);
		return 1;
		/*u8 sdCid[16];
		if (R_FAILED(rc = FSUSER_GetSdmcCid(sdCid, 0x10)))
		{
			printf("FSUSER_GetSdmcCid: %08lX\n", rc);
			reboot(true);
			return 1;
		}
		
		u8 temp = sdCid[0];
		for (int i = 0; i < 15; i++)
		{
			sdCid[i] = sdCid[i + 1];
		}
		sdCid[15] = temp;
		
		u16* sdCid16 = (u16*)sdCid;
		u16 scrambledSdCid[8];
		
		scrambledSdCid[0] = sdCid16[6];
		scrambledSdCid[1] = sdCid16[7];
		scrambledSdCid[2] = sdCid16[4];
		scrambledSdCid[3] = sdCid16[5];
		scrambledSdCid[4] = sdCid16[2];
		scrambledSdCid[5] = sdCid16[3];
		scrambledSdCid[6] = sdCid16[0];
		scrambledSdCid[7] = sdCid16[1];
		
		for (int i = 0; i < 8; i++)
		{
			sprintf(id1 + (4 * i), "%04X", sdCid16[i]);
		}*/
	}
	
	printf("id1: %s\n", id1);
	sprintf(path, "sdmc:/Nintendo 3DS/%s/%s/title0", id0, id1);
	sprintf(path2, "sdmc:/Nintendo 3DS/%s/%s/title", id0, id1);
	
	if (rename(path2, path) != 0)
	{
		perror("rename fail!");
		reboot(true);
		return 1;
	}
	
    if (!(dir = opendir("sdmc:/cartInstallWorkDir")))
    { 
        printf("could not open cartInstallWorkDir\n"); 
        reboot(true);
		return 1;
    } 
  
    while ((de = readdir(dir)) != NULL)
	{
		if (strcmp(".cia", &(de->d_name[strlen(de->d_name) - 4])) == 0)
		{
			sprintf(path, "/cartInstallWorkDir/%s", de->d_name);
			if (R_FAILED(installSdCia(path)))
			{
				printf("installation failed, skipping save copy and cia delete\n");
				continue;
			}
			unlink(path);
			char tidlow[9] = { '\0' };
			memcpy(tidlow, de->d_name, 8);
			sprintf(path, "sdmc:/Nintendo 3DS/%s/%s/title/00040000/%s/data", id0, id1, tidlow);
			sprintf(path2, "sdmc:/Nintendo 3DS/%s/%s/title0/00040000/%s/data", id0, id1, tidlow);
			rename(path, path2);
		}
	}
	
    closedir(dir);
	
	sprintf(path2, "sdmc:/Nintendo 3DS/%s/%s/title", id0, id1);
	
	recursiveDelDir(path2);
	
	sprintf(path, "sdmc:/Nintendo 3DS/%s/%s/title0", id0, id1);
	if (rename(path, path2) != 0)
	{
		perror("rename 2 fail!");
		reboot(true);
		return 1;
	}
	
	reboot(false);
	
	return 0; // Should never get here
}