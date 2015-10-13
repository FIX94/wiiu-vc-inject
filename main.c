/*
 * Copyright (C) 2015 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <conio.h>
#include <windows.h>

enum {
	TYPE_UNK = 0,
	TYPE_SNES = 1,
	TYPE_GBA = 2,
	TYPE_NDS = 3
};

static const int cport = 7331;

void waitforinput();
int SNESchk(unsigned char *buf);
void sendall(SOCKET s, unsigned char *data, size_t size);
void recvall(SOCKET s, unsigned char *data, size_t size);

void main(int argc, char *argv[])
{	//windows exec path is so ugly to get
	char curpath[MAX_PATH]; 
	GetModuleFileName(NULL, curpath, MAX_PATH);
	char *pptr = curpath;
	while(*pptr) *pptr++;
	while(*pptr != '\\') *pptr--;
	*pptr = '\0';
	char ipfile[512];
	sprintf(ipfile, "%s\\ip.txt", curpath);
	//lets actually do cool stuff 8 lines later
	puts("WiiU VC ROM Injector by FIX94");
	FILE *f = fopen(ipfile,"r");
	if(f == NULL)
	{
		printf("%s not found!\n", ipfile);
		waitforinput();
		return;
	}
	//enough space for a IPv4 address
	char caddr[16];
	caddr[15] = '\0';
	fgets(caddr,16,f);
	fclose(f);
	if(argc != 2)
	{
		puts("Please specify a ROM to inject");
		waitforinput();
		return;
	}
	int romType = TYPE_UNK;
	const char *extension = strrchr(argv[1],'.');
	if(extension != NULL)
	{
		if(memcmp(extension,".smc",5) == 0 || memcmp(extension,".sfc",5) == 0 ||
			memcmp(extension,".swc",5) == 0 || memcmp(extension,".fig",5) == 0)
		{
			printf("Got SNES %s extension\n", extension);
			romType = TYPE_SNES;
		}
		else if(memcmp(extension,".gba",5) == 0)
		{
			printf("Got GBA %s extension\n", extension);
			romType = TYPE_GBA;
		}
		else if(memcmp(extension,".nds",5) == 0 || memcmp(extension,".srl",5) == 0)
		{
			printf("Got NDS %s extension\n", extension);
			romType = TYPE_NDS;
		}
		else
			puts("Unknown file extension, guessing type");
	}
	else
		puts("No file extension, guessing type");
	f = fopen(argv[1], "rb");
	if(f == NULL)
	{
		puts("ROM not available!");
		waitforinput();
		return;
	}
	//just read the whole ROM at once, we got RAM nowadays
	fseek(f, 0, SEEK_END);
	size_t fsize = ftell(f);
	rewind(f);
	unsigned char *buf = (unsigned char*)malloc(fsize);
	if(buf == NULL)
	{
		fclose(f);
		puts("Not enough memory!");
		waitforinput();
		return;
	}
	fread(buf,1,fsize,f);
	fclose(f);
	if(romType == TYPE_UNK)
	{
		if(SNESchk(buf+0x7FDC) || SNESchk(buf+0x81DC) ||
			SNESchk(buf+0xFFDC) || SNESchk(buf+0x101DC))
		{
			puts("Guessing SNES file");
			romType = TYPE_SNES;
		}
		else if(*(unsigned int*)(buf+4) == 0x24FFAE51)
		{
			puts("Guessing GBA file");
			romType = TYPE_GBA;
		}
		else if(*(unsigned int*)(buf+0xC0) == 0x24FFAE51)
		{
			puts("Guessing NDS file");
			romType = TYPE_NDS;
		}
		else
		{
			free(buf);
			puts("Unknown file type!");
			waitforinput();
			return;
		}
	}
	//Windows socket init stuff
	WSADATA WsaDat;
	WSAStartup(MAKEWORD(2,2),&WsaDat);
	SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if(s == INVALID_SOCKET)
	{
		puts("Socket error!");
		goto end;
	}
	SOCKADDR_IN saddr;
	saddr.sin_port=htons(cport);
	saddr.sin_family=AF_INET;
	saddr.sin_addr.s_addr=inet_addr(caddr);
	if(connect(s,(SOCKADDR*)(&saddr),sizeof(saddr)) == SOCKET_ERROR)
	{
		printf("Unable to connect to %s:%i\n", caddr, cport);
		goto end;
	}
	printf("Connected to %s:%i\n", caddr, cport);
	unsigned char cmd[1];
	unsigned int dst[2];
	unsigned int addr = 0;
	if(romType == TYPE_SNES)
	{
		//appears to be static
		addr = 0x10502250;
	}
	else
	{
		//cmd_search32
		cmd[0] = 0x72;
		sendall(s, cmd, 1);
		unsigned int search32[3];
		if(romType == TYPE_GBA)
		{
			search32[0] = htonl(0x40000000); //Start Address
			search32[1] = htonl(0xFF2451AE); //Value
			search32[2] = htonl(0x10000000); //Search Size
		}
		else
		{
			search32[0] = htonl(0x28000000); //Start Address
			search32[1] = htonl(0x24FFAE51); //Value
			search32[2] = htonl(0x18000000); //Search Size
		}
		puts("Searching for Nintendo Logo Start");
		sendall(s, (unsigned char*)search32, 12);
		recvall(s, (unsigned char*)&addr, 4);
		addr = ntohl(addr) - ((romType == TYPE_GBA) ? 4 : 0xC0);
		printf("File Header may be at 0x%08x\n",addr);
		if(romType == TYPE_GBA)
		{
			puts("Byteswapping GBA file before injection");
			//Byteswap gba file
			unsigned short *bswap = (unsigned short*)buf;
			unsigned int blocks = fsize/2;
			unsigned int i;
			for(i = 0; i < blocks; i++)
				bswap[i] = __builtin_bswap16(bswap[i]);
		}
	}
	//cmd_upload plus location to send to
	cmd[0] = 0x41;
	sendall(s, cmd, 1);
	dst[0] = htonl(addr);
	dst[1] = htonl(addr+fsize);
	printf("Sending %i bytes to 0x%08x\n", fsize, addr);
	sendall(s, (unsigned char*)dst, 8);
	size_t remain = fsize;
	unsigned char *ptr = buf;
	while(remain > 0)
	{
		size_t sendsize = 0x400;
		if(remain < 0x400) sendsize = remain;
		sendall(s,ptr,sendsize);
		remain -= sendsize;
		ptr += sendsize;
	}
	shutdown(s,SD_SEND); //sent all we have
	recv(s, cmd, 1, 0); //lets see how tcpgecko reacted
	if(cmd[0] == 0xAA) printf("Transfer successful (%02x)!\n", cmd[0]);
	else printf("Transfer error occured (%02x)!\n", cmd[0]);
end:
	closesocket(s);
	WSACleanup();
	free(buf);
	waitforinput();
}

//simple socket send helper functions
void sendall(SOCKET s, unsigned char *data, size_t size)
{
	size_t remain = size;
	while(remain > 0)
	{
		int sent = send(s, data, remain, 0);
		remain -= sent;
		data += sent;
	}
}

void recvall(SOCKET s, unsigned char *data, size_t size)
{
	size_t remain = size;
	while(remain > 0)
	{
		int recvd = recv(s, data, remain, 0);
		remain -= recvd;
		data += recvd;
	}
}

//cant get simpler than that
void waitforinput()
{
	puts("Press any key to exit");
	getch();
}

//Checksum verification
int SNESchk(unsigned char *buf)
{
	unsigned short chk1 = (*(unsigned short*)buf);
	unsigned short chk2 = ~(*(unsigned short*)(buf+2));
	return chk1 == chk2;
}
