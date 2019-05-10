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
		printf("AM_DeleteAppTitle: %08lX\n", rc);
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
	
	if(R_FAILED(init()))
	{
		printf("Init failed!\n");
		reboot(true);
		return 1;
	}
	
	struct dirent* de;
	char path[300];
   
    DIR* dr = opendir("sdmc:/dummycias"); 
  
    if (!dr)
    { 
        printf("Could not open dummycias dir"); 
        reboot(true);
		return 1;
    } 
  
    while ((de = readdir(dr)) != NULL)
	{
		if (strcmp(".cia", &(de->d_name[strlen(de->d_name) - 4])) == 0)
		{
			sprintf(path, "/dummycias/%s", de->d_name);
			if (R_FAILED(installSdCia(path)))
				printf("Installation failed, proceeding regardless\n");
		}
	}
	
    closedir(dr);
	
	reboot(false);
	
	return 0; // Should never get here
}