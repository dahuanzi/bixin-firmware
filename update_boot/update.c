/*
 * libusb example program to list devices on the bus
 * Copyright © 2007 Daniel Drake <dsd@gentoo.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "device.h"

#define BHA_VID 0X1209
#define BHA_PID 0X53c1
#define DEV_NAME "trezor"


struct libusb_device_handle *g_dev = NULL;

//MASS STORAGE
#define ENDPOINT_OUT	0X01
#define ENDPOINT_IN		0X82

#define RETRY_MAX                     5
#define REQUEST_SENSE_LENGTH          0x12
#define INQUIRY_LENGTH                0x24
#define READ_CAPACITY_LENGTH          0x08

// Mass Storage Requests values. See section 3 of the Bulk-Only Mass Storage Class specifications
#define BOMS_RESET                    0xFF
#define BOMS_GET_MAX_LUN              0xFE

// Section 5.1: Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

// Section 5.2: Command Status Wrapper (CSW)
struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

static const uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,10,10,10,  //  F
};

static int send_mass_storage_command(libusb_device_handle *handle, uint8_t endpoint, uint8_t lun,
	uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i, r, size,m;
	uint8_t *p;
	struct command_block_wrapper cbw;

	p=(uint8_t*)&cbw;

	if (cdb == NULL) {
		return -1;
	}

	if (endpoint & LIBUSB_ENDPOINT_IN) {
		printf("send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}

	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw.CBWCB))) {
		printf("send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}

	memset(&cbw, 0, sizeof(cbw));
	cbw.dCBWSignature[0] = 'U';
	cbw.dCBWSignature[1] = 'S';
	cbw.dCBWSignature[2] = 'B';
	cbw.dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw.dCBWTag = tag++;
	cbw.dCBWDataTransferLength = data_length;
	cbw.bmCBWFlags = direction;
	cbw.bCBWLUN = lun;
	// Subclass is 1 or 6 => cdb_len
	cbw.bCBWCBLength = cdb_len;
	memcpy(cbw.CBWCB, cdb, cdb_len);
	
	i = 0;
	do {
		// The transfer length must always be exactly 31 bytes.
		r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&cbw, 31, &size, 1000);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (r != LIBUSB_SUCCESS) {
		printf("   send_mass_storage_command: %s\n", libusb_strerror((enum libusb_error)r));
		return -1;
	}

//	printf("   sent %d CDB bytes\n", cdb_len);
	return 0;
}
static int get_mass_storage_status(libusb_device_handle *handle, uint8_t endpoint, uint32_t expected_tag)
{
	int i, r, size;
	struct command_status_wrapper csw;

	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
	i = 0;
	do {
		r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&csw, 13, &size, 1000);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (r != LIBUSB_SUCCESS) {
		printf("   get_mass_storage_status: %s\n", libusb_strerror((enum libusb_error)r));
		return -1;
	}
	if (size != 13) {
		printf("   get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw.dCSWTag != expected_tag) {
		printf("   get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw.dCSWTag);
		return -1;
	}
	// For this test, we ignore the dCSWSignature check for validity...
	//printf("   Mass Storage Status: %02X (%s)\n", csw.bCSWStatus, csw.bCSWStatus?"FAILED":"Success");
	if (csw.dCSWTag != expected_tag)
		return -1;
	if (csw.bCSWStatus) {
		// REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
		// command failed somehow.  Larger values (2 in particular) mean that
		// the command couldn't be understood.
		if (csw.bCSWStatus == 1)
			return -2;	// request Get Sense
		else
			return -1;
	}

	// In theory we also should check dCSWDataResidue.  But lots of devices
	// set it wrongly.
	return 0;
}

int enum_device(char *dev_list,uint32_t *out_len)
{
	libusb_device **devs;
	int r,i;
	ssize_t cnt;
	struct libusb_device_descriptor desc;
	uint32_t len=0;

	r = libusb_init(NULL);
	if (r < 0)
		return r;

	cnt = libusb_get_device_list(NULL, &devs);
	if (cnt < 0)
		return (int) cnt;
	
	for (i = 0; devs[i]; ++i) {
		
		int r = libusb_get_device_descriptor(devs[i], &desc);
		if (r < 0) {			
			return r;
		}
        // printf("vid= %04x\r\n",desc.idVendor);
        // printf("pid= %04x\r\n",desc.idProduct);
		if(desc.idVendor==BHA_VID&&desc.idProduct==BHA_PID){
            printf("get device\r\n");
			memcpy(dev_list+len,DEV_NAME,strlen(DEV_NAME));
			len+=strlen(DEV_NAME);
			dev_list[len++]='\x0';
		}
	}
	dev_list[len++]='\x0';
	*out_len=len;
	libusb_free_device_list(devs, 1);
	libusb_exit(NULL);
	return 0;	
}

int open_device(char* dev_name,HDEV *device)
{
	int ret;	

	if(strcmp(dev_name,DEV_NAME)==0){
		ret = libusb_init(NULL);
		if (ret < 0)
			return ret;

		g_dev = libusb_open_device_with_vid_pid(NULL, BHA_VID, BHA_PID);
		if(g_dev==NULL){
			return -1;
		}
		ret=libusb_set_auto_detach_kernel_driver(g_dev, 1);
		if(ret!=LIBUSB_SUCCESS){
			libusb_close(g_dev);
			libusb_exit(NULL);
			return -2;
		}
		
		ret = libusb_claim_interface(g_dev, 0);
		if (ret < 0) {
			//fprintf(stderr, "usb_claim_interface error %d\n", r);
			libusb_close(g_dev);
			libusb_exit(NULL);
			return -3;
		}
	
		*device=g_dev;
		return 0;
	}else{
		return -4;
	}
}

int close_device(HDEV device)
{
	libusb_device_handle *handle=(libusb_device_handle *)device;
	libusb_close(handle);
	libusb_exit(NULL);
}
int command_transmit(HDEV device,uint8_t *in_buf,uint32_t in_len,uint8_t *out_buf,uint32_t *out_len)
{
	int r, size;
	uint32_t expected_tag;
	uint32_t i;
	uint8_t cdb[16];	// SCSI Command Descriptor Block
	uint8_t buffer[2048];

	libusb_device_handle *handle=(libusb_device_handle *)device;
	*out_len=0;

	// Send Command
	printf("Sending Command:\n");
	memset(buffer, 0, sizeof(buffer));
	memset(cdb, 0, sizeof(cdb));
	
	cdb[0] = 0xFE;	// 
	cdb[1] = 0x80;
	cdb[2] = 0xf8;
	cdb[5] = (in_len>>24)&0xff;
	cdb[6] = (in_len>>16)&0xff;
	cdb[7] = (in_len>>8)&0xff;
	cdb[8] = (in_len)&0xff;

	memcpy(buffer,in_buf,in_len);

	r=send_mass_storage_command(handle, ENDPOINT_OUT, 0, cdb, LIBUSB_ENDPOINT_OUT, in_len, &expected_tag);
	if (r < 0) {
		fprintf(stderr, "send_mass_storage_command err");
		return -1;
	}
	r=libusb_bulk_transfer(handle, ENDPOINT_OUT, (uint8_t*)&buffer, in_len, &size, 0);
	if (r < 0) {
		fprintf(stderr, "libusb_bulk_transfer err");
		return -1;
	}
	r=get_mass_storage_status(handle, ENDPOINT_IN, expected_tag);
	if (r < 0) {
		fprintf(stderr, "get_mass_storage_status err");
		return -1;
	}
	//Send Resp 
	printf("Geting Resp:\n");
	memset(buffer, 0, sizeof(buffer));
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0xFd;	// 
	cdb[1] = 0x80;
	cdb[2] = 0xf9;

	r=send_mass_storage_command(handle, ENDPOINT_OUT, 0, cdb, LIBUSB_ENDPOINT_IN, sizeof(buffer), &expected_tag);
	if (r < 0) {
		fprintf(stderr, "send_mass_storage_command err");
		return -1;
	}
	r=libusb_bulk_transfer(handle, ENDPOINT_IN, (uint8_t*)&buffer, sizeof(buffer), &size, 0);
	if (r < 0) {
		fprintf(stderr, "libusb_bulk_transfer err");
		return -1;
	}
	r=get_mass_storage_status(handle, ENDPOINT_IN, expected_tag);
	if (r < 0) {
		fprintf(stderr, "get_mass_storage_status err");
		return -1;
	}
	for(i=0;i<size;i++)
		printf("%02x",buffer[i]);
	printf("\n");
	memcpy(out_buf,buffer,size);
	*out_len=size;

	return 0;
}
int transfer(HDEV device,uint8_t *in_buf,int in_len)
{
    uint8_t cmd[128];
    int len;
    int r;
    memcpy(cmd,in_buf,in_len);
    libusb_device_handle *handle=(libusb_device_handle *)device;
    r=libusb_interrupt_transfer(handle,0x01,cmd,in_len,&len,1000);

	r=libusb_interrupt_transfer(handle,0x81,cmd,in_len,&len,1000);

	return r;
}
#if 0
int main(void)
{
	int ret,i;
	char dev_list[256]={0};
	uint8_t resp[2048];
	uint32_t list_len,resp_len;
	HDEV device;

	ret=enum_device(dev_list,&list_len);
	if(ret==0){
		printf("%s",dev_list);
		ret=open_device(dev_list,&device);
		if(ret==0){
			printf("open device success\n");
		}else{
			printf("open device fail\n");
		}
		transfer(device,"\x3f\x23\x23",75);
	
	}	
	
	return 0;
}
#else
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>

int main(void)
{
	int ret,i;
	char dev_list[256]={0};
	uint32_t list_len;
	uint8_t buf[64];
	HDEV device;

	int fd=0;
	struct stat sta;

	ret=enum_device(dev_list,&list_len);
	if(ret==0){
		printf("%s",dev_list);
		ret=open_device(dev_list,&device);
		if(ret==0){
			printf("open device success\n");
		}else{
			printf("open device fail\n");
			return 0;
		}
	
	}	
	fd=open("./bootloader.bin",O_RDONLY);
	if(fd==-1){
		printf("open file fail\n");	
		return 0;
	}
		
	if(fstat(fd,&sta)==-1){
		printf("get stat fail\n");	
		return 0;
	}
	if(sta.st_size!=32768){
		printf("file length error\n");	
		return 0;
	}

	printf("file length= %ld\n",sta.st_size);

	int total_len,offset=0;
	int percent;
	total_len=sta.st_size;

	printf("start update ...\n");

	if(transfer(device,"\x01",1)==0){
		while(total_len){
			percent=offset*100/sta.st_size;
			printf("write file  %d %%",percent);
			printf("\r");			
			read(fd,buf,64);
			transfer(device,buf,64);
			total_len-=64;
			offset+=64;
		}		
	}else{
		printf("enter boot failure\n");
	}
	printf("\n");	
	printf("update done\n");
	return 0;
}

#endif

