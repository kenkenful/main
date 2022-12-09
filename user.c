#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/types.h>


#define MAJOR_NUM 100


enum nvme_constants {
	NVME_NSID_ALL			= 0xffffffff,
	NVME_NSID_NONE			= 0,
	NVME_UUID_NONE			= 0,
	NVME_CNTLID_NONE		= 0,
	NVME_NVMSETID_NONE		= 0,
	NVME_LOG_LSP_NONE		= 0,
	NVME_LOG_LSI_NONE		= 0,
	NVME_LOG_LPO_NONE		= 0,
	NVME_IDENTIFY_DATA_SIZE		= 4096,
	NVME_ID_NVMSET_LIST_MAX		= 31,
	NVME_ID_UUID_LIST_MAX		= 127,
	NVME_ID_CTRL_LIST_MAX		= 2047,
	NVME_ID_NS_LIST_MAX		= 1024,
	NVME_ID_SECONDARY_CTRL_MAX	= 127,
	NVME_ID_ND_DESCRIPTOR_MAX	= 16,
	NVME_FEAT_LBA_RANGE_MAX		= 64,
	NVME_LOG_ST_MAX_RESULTS		= 20,
	NVME_LOG_TELEM_BLOCK_SIZE	= 512,
	NVME_DSM_MAX_RANGES		= 256,
	NVME_NQN_LENGTH			= 256,
	NVMF_TRADDR_SIZE		= 256,
	NVMF_TSAS_SIZE			= 256,
	NVME_ZNS_CHANGED_ZONES_MAX	= 511,
};


enum nvme_identify_cns {
	NVME_IDENTIFY_CNS_NS					= 0x00,
	NVME_IDENTIFY_CNS_CTRL					= 0x01,
	NVME_IDENTIFY_CNS_NS_ACTIVE_LIST			= 0x02,
	NVME_IDENTIFY_CNS_NS_DESC_LIST				= 0x03,
	NVME_IDENTIFY_CNS_NVMSET_LIST				= 0x04,
	NVME_IDENTIFY_CNS_CSI_NS				= 0x05, /* XXX: Placeholder until assigned */
	NVME_IDENTIFY_CNS_CSI_CTRL				= 0x06, /* XXX: Placeholder until assigned */
	NVME_IDENTIFY_CNS_ALLOCATED_NS_LIST			= 0x10,
	NVME_IDENTIFY_CNS_ALLOCATED_NS				= 0x11,
	NVME_IDENTIFY_CNS_NS_CTRL_LIST				= 0x12,
	NVME_IDENTIFY_CNS_CTRL_LIST				= 0x13,
	NVME_IDENTIFY_CNS_PRIMARY_CTRL_CAP			= 0x14,
	NVME_IDENTIFY_CNS_SECONDARY_CTRL_LIST			= 0x15,
	NVME_IDENTIFY_CNS_NS_GRANULARITY			= 0x16,
	NVME_IDENTIFY_CNS_UUID_LIST				= 0x17,
	NVME_IDENTIFY_CNS_CSI_ALLOCATED_NS			= 0x18, /* XXX: Placeholder until assigned */
};


// ioctls

/* Admin commands */
enum nvme_admin_opcode {
	nvme_admin_delete_sq		= 0x00,
	nvme_admin_create_sq		= 0x01,
	nvme_admin_get_log_page		= 0x02,
	nvme_admin_delete_cq		= 0x04,
	nvme_admin_create_cq		= 0x05,
	nvme_admin_identify		= 0x06,
	nvme_admin_abort_cmd		= 0x08,
	nvme_admin_set_features		= 0x09,
	nvme_admin_get_features		= 0x0a,
	nvme_admin_async_event		= 0x0c,
	nvme_admin_ns_mgmt		= 0x0d,
	nvme_admin_activate_fw		= 0x10,
	nvme_admin_download_fw		= 0x11,
	nvme_admin_ns_attach		= 0x15,
	nvme_admin_keep_alive		= 0x18,
	nvme_admin_directive_send	= 0x19,
	nvme_admin_directive_recv	= 0x1a,
	nvme_admin_dbbuf		= 0x7C,
	nvme_admin_format_nvm		= 0x80,
	nvme_admin_security_send	= 0x81,
	nvme_admin_security_recv	= 0x82,
	nvme_admin_sanitize_nvm		= 0x84,
};

/* I/O commands */
enum nvme_opcode {
	nvme_cmd_flush		= 0x00,
	nvme_cmd_write		= 0x01,
	nvme_cmd_read		= 0x02,
	nvme_cmd_write_uncor	= 0x04,
	nvme_cmd_compare	= 0x05,
	nvme_cmd_write_zeroes	= 0x08,
	nvme_cmd_dsm		= 0x09,
	nvme_cmd_resv_register	= 0x0d,
	nvme_cmd_resv_report	= 0x0e,
	nvme_cmd_resv_acquire	= 0x11,
	nvme_cmd_resv_release	= 0x15,
};

struct nvme_sgl_desc {
	__le64	addr;
	__le32	length;
	__u8	rsvd[3];
	__u8	type;
};

struct nvme_keyed_sgl_desc {
	__le64	addr;
	__u8	length[3];
	__u8	key[4];
	__u8	type;
};

union nvme_data_ptr {
	struct {
		__le64	prp1;
		__le64	prp2;
	};
	struct nvme_sgl_desc	sgl;
	struct nvme_keyed_sgl_desc ksgl;
};

struct nvme_common_command {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__le32			cdw2[2];
	__le64			metadata;
	union nvme_data_ptr	dptr;
	__le32			cdw10[6];
};

struct nvme_rw_command {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2;
	__le64			metadata;
	union nvme_data_ptr	dptr;
	__le64			slba;
	__le16			length;
	__le16			control;
	__le32			dsmgmt;
	__le32			reftag;
	__le16			apptag;
	__le16			appmask;
};

struct nvme_passthru_cmd {
	__u8	opcode;
	__u8	flags;
	__u16	rsvd1;
	__u32	nsid;
	__u32	cdw2;
	__u32	cdw3;
	__u64	metadata;
	__u64	addr;
	__u32	metadata_len;
	__u32	data_len;
	__u32	cdw10;
	__u32	cdw11;
	__u32	cdw12;
	__u32	cdw13;
	__u32	cdw14;
	__u32	cdw15;
	__u32	timeout_ms;
	__u32	result;
};

struct nvme_command {
	union {
		struct nvme_common_command common;
		struct nvme_rw_command rw;
#if 0
		struct nvme_identify identify;
		struct nvme_features features;
		struct nvme_create_cq create_cq;
		struct nvme_create_sq create_sq;
		struct nvme_delete_queue delete_queue;
		struct nvme_download_firmware dlfw;
		struct nvme_format_cmd format;
		struct nvme_dsm_cmd dsm;
		struct nvme_write_zeroes_cmd write_zeroes;
		struct nvme_abort_cmd abort;
		struct nvme_get_log_page_command get_log_page;
		struct nvmf_common_command fabrics;
		struct nvmf_connect_command connect;
		struct nvmf_property_set_command prop_set;
		struct nvmf_property_get_command prop_get;
		struct nvme_dbbuf dbbuf;
		struct nvme_directive_cmd directive;
#endif
	};
};

#define NVME 'N'
#define IOCTL_ADMIN_CMD _IOW(NVME, 1, struct nvme_command*)
#define IOCTL_IO_CMD _IOW(NVME, 2, struct nvme_command*)
#define IOCTL_MAP_PAGE _IOW(NVME, 3, struct nvme_command*)


#define NVME_IOCTL_ID		_IO('N', 0x40)
#define NVME_IOCTL_RESET	_IO('N', 0x44)
#define NVME_IOCTL_SUBSYS_RESET	_IO('N', 0x45)
#define NVME_IOCTL_RESCAN	_IO('N', 0x46)
#define NVME_IOCTL_ADMIN_CMD	_IOWR('N', 0x41, struct nvme_passthru_cmd)
#define NVME_IOCTL_IO_CMD	_IOWR('N', 0x43, struct nvme_passthru_cmd)
#define NVME_IOCTL_ADMIN64_CMD  _IOWR('N', 0x47, struct nvme_passthru_cmd64)
#define NVME_IOCTL_IO64_CMD     _IOWR('N', 0x48, struct nvme_passthru_cmd64)




int main(int argc, char *argv[]){
    struct nvme_command cmd = {0};
	unsigned char u8Buf[819200];
	int rc;
	int iCounter=0;
	unsigned char *p;

	unsigned long pagesize = getpagesize();

	//int ret = posix_memalign((void*)&p, sizeof(void*), 1024*800);
	int ret = posix_memalign((void*)&p, 4096, 1024*800);

	unsigned long addr = (unsigned long)p;

	memset(p, 0xee, 1024*800);

	//printf("%ld \n", sizeof(void*));
	//printf("%ld \n", addr%4096);

    int fd = open("/dev/nvmet0", O_RDWR|O_SYNC);
    if(fd < 0){
        printf("open error\n");
        return 1;
    }

	int id;


#if 0
	id = 0;

	struct nvme_passthru_cmd cmd = {
		.opcode		= nvme_admin_identify,
		.nsid		= NVME_NSID_NONE,
		.addr		= (__u64)p,
		.data_len	= NVME_IDENTIFY_DATA_SIZE,
		.cdw10		= NVME_IDENTIFY_CNS_CTRL,
		//.cdw11		= cdw11,
		//.cdw14		= cdw14,
	};




	rc = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);

    if(rc <0){printf("ioctl fail\n");}


	for(int i = 0; i< 16; ++i)
		printf("%x ",  p[i]);

	printf("\n");
#else 

	id = 0;
	cmd.rw.opcode = nvme_cmd_write;
    cmd.rw.nsid = 1;
    cmd.rw.slba = 11;
    cmd.rw.length = 7;   // number of logical blocks     page sizeをこえないように設定する。
	cmd.rw.dptr.prp1 = (__le64)p; 
    cmd.rw.command_id = id;  

	rc = ioctl(fd, IOCTL_MAP_PAGE, &cmd);

    if(rc <0){printf("ioctl fail\n");}


	sleep(1);
	memset(p, 0x0, 1024*800);

	id = 1;
	cmd.rw.opcode = nvme_cmd_read;
    cmd.rw.nsid = 1;
    cmd.rw.slba = 11;
    cmd.rw.length = 7;   // number of logical blocks     page sizeをこえないように設定する。
	cmd.rw.dptr.prp1 = (__le64)p; 
    cmd.rw.command_id = id;  

	rc = ioctl(fd, IOCTL_MAP_PAGE  , &cmd);

    if(rc <0){printf("ioctl fail\n");}

	printf("0x%x\n",p[0]);

#endif


    close(fd);
	free(p);
	return 0;
}