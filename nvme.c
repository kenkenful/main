#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/dma.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <linux/nvme.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/wait.h>
#include "nvme.h"

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Simple NVMe Device Driver");

#define MAJOR_NUM 100
#define DEVICE_NAME "nvmet"
#define NVME_MINORS		(1U << MINORBITS)


static DEFINE_IDA(nvme_instance_ida);

static struct class* nvme_class;
static dev_t nvme_chr_devt;   //major番号を動的に割り当てるために必要

#define PCI_VENDOR_ID_TEST 0x144d
#define PCI_DEVICE_ID_TEST 0xa808

#define TEST_PCI_DRIVER_DEBUG
#ifdef  TEST_PCI_DRIVER_DEBUG
#define tprintk(fmt, ...) printk(KERN_ALERT "** (%3d) %-20s: " fmt, __LINE__,  __func__,  ## __VA_ARGS__)
#else
#define tprintk(fmt, ...) 
#endif

#define SQ_SIZE(depth)		(depth * sizeof(struct nvme_command))
#define CQ_SIZE(depth)		(depth * sizeof(struct nvme_completion))
#define QUEUE_DEPTH (32)   // Admin Queue とIO Queueのdepthは32とする。
#define TIMEOUT (6*HZ)

#define NUM_OF_IO_QUEUE_PAIR (4)

#define MESSAGE_QUEUE_DEPTH (1024)

struct nvme_submission_message_queue{
    unsigned int head;
    unsigned int tail;
    struct nvme_command c[MESSAGE_QUEUE_DEPTH];
    struct timespec time[MESSAGE_QUEUE_DEPTH];
};

static struct nvme_submission_message_queue start_message_queue = {.head = 0, .tail = 0,};

struct nvme_completion_message_queue{
    unsigned int head;
    unsigned int tail;
    struct nvme_completion c[MESSAGE_QUEUE_DEPTH];
    struct timespec time[MESSAGE_QUEUE_DEPTH];
};

static struct nvme_completion_message_queue end_message_queue = {.head = 0, .tail = 0,};

//#define TIMER_CNT (200)
//static struct timer_list timerl;

#if 0 
static void timer_handler(struct timer_list *t){
    while(end_message_queue. head != end_message_queue. tail){
        tprintk("%.2lu:%.2lu:%.2lu:%.6lu: Command id: 0x%x,  Status: 0x%x, phase tag %d\n", 
                (end_message_queue.time[end_message_queue. tail].tv_sec / 3600) % (24),
                (end_message_queue.time[end_message_queue. tail].tv_sec / 60) % (60),
                end_message_queue.time[end_message_queue. tail].tv_sec % 60,
                end_message_queue.time[end_message_queue. tail].tv_nsec / 1000,
                le16_to_cpu(end_message_queue.c[end_message_queue. tail].command_id), 
                (le16_to_cpu(end_message_queue.c[end_message_queue. tail].status) & 0xfe) >> 1, 
                le16_to_cpu(end_message_queue.c[end_message_queue. tail].status) & 1);
        if(++end_message_queue. tail == MESSAGE_QUEUE_DEPTH){
            end_message_queue. tail = 0;        
        }
    }

    while(start_message_queue. head != start_message_queue. tail){
        tprintk("%.2lu:%.2lu:%.2lu:%.6lu: Command id: 0x%x\n", 
                (start_message_queue.time[start_message_queue. tail].tv_sec / 3600) % (24),
                (start_message_queue.time[start_message_queue. tail].tv_sec / 60) % (60),
                start_message_queue.time[start_message_queue. tail].tv_sec % 60,
                start_message_queue.time[start_message_queue. tail].tv_nsec / 1000,
                le16_to_cpu(start_message_queue.c[start_message_queue. tail].common.command_id));
        if(++start_message_queue. tail == MESSAGE_QUEUE_DEPTH){
            start_message_queue. tail = 0;        
        }
    }
    //timerl.expires = jiffies + TIMER_CNT;
    //add_timer(&timerl);
}
#endif


struct nvme_queue{
    struct nvme_command *sq_cmds;           // SQの仮想アドレス
    volatile struct nvme_completion *cqes;  // CQの仮想アドレス

	struct nvme_command __iomem *sq_cmds_io;
    struct nvme_dev *pnvme_dev;

    u32 __iomem *q_db;
    u16 q_depth;
	s16 cq_vector;
    u16 sq_head;
	u16 sq_tail;
	u16 cq_head;
    //u16 cq_tail;   //管理しなくてよさそう。
	u16 qid;
	u8 cq_phase;
	u8 cqe_seen;
    u32 *dbbuf_sq_db; 
	u32 *dbbuf_cq_db;
	u32 *dbbuf_sq_ei;
	u32 *dbbuf_cq_ei;

    spinlock_t q_lock;

    dma_addr_t sq_dma_addr;    // SQの物理アドレス
	dma_addr_t cq_dma_addr;    // CQの物理アドレス　
};

struct nvme_dev{
    struct pci_dev *pdev;
    struct cdev cdev;
    dev_t  devt;
    int instance;
    bool is_open;
    int io_qid;

    // for PCI pio/mmio
    unsigned long long mmio_base, mmio_flags, mmio_length;
    void __iomem *bar;

    unsigned int pio_memsize;
    struct nvme_queue *padminQ;
    struct nvme_queue *pioQ[NUM_OF_IO_QUEUE_PAIR];

    struct dma_pool *prp_page_pool;
    //struct dma_pool *prp_page_pool_first; // 最初のprp list
    //struct dma_pool *prp_page_pool_later; // 2番め以降のprp list

    //identify が応答するかお試しで作成したData Buffer
    dma_addr_t prp1_dma_addr;   //物理アドレス   
    void* prp1_virt_addr;     // 仮想アドレス

      int wait_condition;
    wait_queue_head_t sdma_q;

};

struct sg_mapping{
  long npages;
  struct page ** pages;
  struct scatterlist * sgl;
  enum dma_data_direction dir;
  unsigned long num_sg;
  struct nvme_dev* dev;
} ;

// ioctls
#define NVME 'N'
#define IOCTL_ADMIN_CMD _IOW(NVME, 1, struct nvme_command*)
#define IOCTL_IO_CMD _IOW(NVME, 2, struct nvme_command*)
#define IOCTL_MAP_PAGE _IOW(NVME, 3, struct nvme_command*)

static struct pci_device_id test_nvme_ids[] =
{
    { PCI_DEVICE(PCI_VENDOR_ID_TEST, PCI_DEVICE_ID_TEST) },
    { 0, },
};

MODULE_DEVICE_TABLE(pci, test_nvme_ids);

static void release_pinned(struct page **pages, long npages){
  int i;
  for(i=0; i<npages; i++)
    put_page(pages[i]);
}

// 同期でコマンドを発行する関数
static long submitCmd(struct nvme_command *c, struct nvme_queue *q){

    spin_lock(&q->q_lock);
    u16 tail = q->sq_tail;
    long val;
    struct nvme_dev *p = q->pnvme_dev;

    memcpy(&q->sq_cmds[tail], c, sizeof(struct nvme_command));
    if (++tail == q->q_depth){
        tail = 0;
    }
    writel(tail, p-> bar + NVME_REG_DBS + 8 * q->qid); //ドアベルレジスタを叩く
    q->sq_tail = tail;

    struct timespec time;
    getnstimeofday(&time);
    memcpy(&start_message_queue.time[start_message_queue.head], &time, sizeof(struct timespec));
    memcpy(&start_message_queue.c[start_message_queue.head], c, sizeof(struct nvme_command));
    if(++start_message_queue.head == MESSAGE_QUEUE_DEPTH){
        start_message_queue.head = 0;
    }

    spin_unlock(&q->q_lock);
    return val;
}

static int nvmet_pci_open(struct inode* inode, struct file* filp){
    struct nvme_dev *pnvme_dev =container_of(inode->i_cdev, struct nvme_dev, cdev);
    filp->private_data = pnvme_dev;
    pnvme_dev->is_open = 1;
    return 0;
}

static int nvmet_pci_close(struct inode* inode, struct file* filp){
    struct nvme_dev *pnvme_dev = filp->private_data;
    pnvme_dev->is_open = 0;
    return 0;
}

#if 0
static int alloc_common_buffer(struct nvme_dev *pnvme_dev){
    unsigned long npages_req = 0;

    struct page **pages = NULL;
    unsigned long udata;
    unsigned long len;
    long npages = 0;
    unsigned fp_offset; // first page offset

    int i, j;
    int start_size;
    unsigned page_len;

    unsigned long len_rem;
    int dma_len;
    u64 dma_addr;
    unsigned long num_sg;
    
    dma_addr_t prp_dma;
    __le64 *prp_list, *old_prp_list;
    enum dma_data_direction dir;

    dir = ((pcmd -> rw.opcode & 1) == 1) ? DMA_FROM_DEVICE : DMA_TO_DEVICE; 
    len = (pcmd -> rw.length + 1) << 9;  // 512フォーマットだとして
    if(len == 0) return -EINVAL;
    udata = (unsigned long)pcmd -> rw.dptr.prp1;
    len_rem = len;

    tprintk("pages: %ld\n", DIV_ROUND_UP(offset_in_page(udata) + len, PAGE_SIZE));
    npages_req = ((udata + len - 1)>>PAGE_SHIFT) - (udata>>PAGE_SHIFT) + 1;
    tprintk("pages: %ld\n", npages_req);

    if ((pages = kmalloc(npages_req * sizeof(*pages), GFP_KERNEL)) == NULL){
        printk(KERN_ERR "Failure kmalloc.\n");
        return -EINVAL;
    }

    // Pin pages
    down_read(&current->mm->mmap_sem);
    npages = get_user_pages(udata, npages_req, (dir==DMA_FROM_DEVICE) ? FOLL_WRITE : 0, pages, NULL);

    if (npages <= 0){
        printk(KERN_ERR "Failure Pinning Memory Page.\n");
        kfree(pages);
        //kfree(sg_map);
        return -EINVAL;
    }

    up_read(&current->mm->mmap_sem);

    fp_offset = (udata & (~PAGE_MASK));
    tprintk("offset %ld\n", fp_offset);
    tprintk("offset %ld\n", offset_in_page(udata));
    //tprintk("unsigned long %d\n", sizeof(unsigned long));

    start_size = PAGE_SIZE-fp_offset;

    //各Pageをdma mappingして PRP LISTを作成する。 64bitのPCなので、page構造体のprivate変数にdmaアドレスを入れる。
    for(i=0; i<npages; i++){
        page_len = ((fp_offset+len_rem) > PAGE_SIZE ? (PAGE_SIZE-fp_offset) : len_rem);

        set_page_private(pages[i], dma_map_page(&pnvme_dev -> pdev-> dev, pages[i], fp_offset, page_len, dir));
        //addrs[i] = dma_map_page(&pnvme_dev -> pdev-> dev, pages[i], fp_offset, page_len, DMA_FROM_DEVICE);

        tprintk("DMA address %lld\n", pages[i]-> private);

        if (dma_mapping_error(&pnvme_dev -> pdev-> dev, page_private(pages[i]))) {
            tprintk("BAD DMA mapping\n");
            for(j=0; j<i; j++){
                if(j==0) dma_unmap_page(&pnvme_dev -> pdev-> dev, page_private(pages[j]), start_size, dir);
                else dma_unmap_page(&pnvme_dev -> pdev-> dev, page_private(pages[j]), PAGE_SIZE, dir);
            }
            release_pinned(pages, npages);
            kfree(pages);
            return -EINVAL;
	    }
        len_rem -= page_len;
        fp_offset = 0;

    }

    for(i=0; i<npages; i++){
        if(i==0) dma_unmap_page(&pnvme_dev -> pdev-> dev, page_private(pages[i]), start_size, dir);
        else dma_unmap_page(&pnvme_dev -> pdev-> dev, page_private(pages[i]), PAGE_SIZE, dir);
    }

map_error_handling:

    release_pinned(pages, npages);
    kfree(pages);
    return 0;

}


static void free_common_buffer(struct nvme_dev *pnvme_dev){




    return 0;
}

#endif

static int set_prps(struct nvme_command *pcmd, struct nvme_dev *pnvme_dev){
    unsigned long npages_req = 0;

    struct page **pages = NULL;
    unsigned long udata;
    unsigned long len;
    long npages = 0, npages_list = 0;
    unsigned fp_offset; // first page offset

    int i, j, k=0;
    int start_size;
    unsigned page_len;

    unsigned long len_rem;
    int dma_len;
    u64 dma_addr;
    unsigned long num_sg;

    //struct dma_pool *pool;
    dma_addr_t prp_dma;
    __le64 *prp_list, *old_prp_list;
    enum dma_data_direction dir;
    int list_max;
    long val;
    __le64 *list[64];

    pnvme_dev->wait_condition = 0;

    dir = ((pcmd -> rw.opcode & 1) == 1) ? DMA_TO_DEVICE: DMA_FROM_DEVICE; 
    len = (pcmd -> rw.length + 1) << 9;  // 512フォーマットだとして
    if(len == 0) return -EINVAL;
    udata = (unsigned long)pcmd -> rw.dptr.prp1;
    len_rem = len;

    tprintk("pages: %ld\n", DIV_ROUND_UP(offset_in_page(udata) + len, PAGE_SIZE));
    npages_req = ((udata + len - 1)>>PAGE_SHIFT) - (udata>>PAGE_SHIFT) + 1;
    //tprintk("pages: %ld\n", npages_req);

    if ((pages = kmalloc(npages_req * sizeof(*pages), GFP_KERNEL)) == NULL){
        printk(KERN_ERR "Failure kmalloc.\n");
        return -EINVAL;
    }

    // Pin pages
    down_read(&current->mm->mmap_sem);
    npages = get_user_pages(udata, npages_req, (dir==DMA_FROM_DEVICE) ? FOLL_WRITE : 0, pages, NULL);

    if (npages <= 0){
        printk(KERN_ERR "Failure Pinning Memory Page.\n");
        kfree(pages);
        //kfree(sg_map);
        return -EINVAL;
    }

    up_read(&current->mm->mmap_sem);

    fp_offset = (udata & (~PAGE_MASK));
    tprintk("offset %ld\n", fp_offset);
    //tprintk("offset %ld\n", offset_in_page(udata));
    //tprintk("unsigned long %d\n", sizeof(unsigned long));

    start_size = PAGE_SIZE-fp_offset;

    //各Pageをdma mappingして PRP LISTを作成する。 64bitのPCなので、page構造体のprivate変数にdmaアドレスを入れる。
    for(i=0; i<npages; i++){
        page_len = ((fp_offset+len_rem) > PAGE_SIZE ? (PAGE_SIZE-fp_offset) : len_rem);

        set_page_private(pages[i], dma_map_page(&pnvme_dev -> pdev-> dev, pages[i], fp_offset, page_len, dir));
        //addrs[i] = dma_map_page(&pnvme_dev -> pdev-> dev, pages[i], fp_offset, page_len, DMA_FROM_DEVICE);

        tprintk("DMA address %lld\n", pages[i]-> private);

        if (dma_mapping_error(&pnvme_dev -> pdev-> dev, page_private(pages[i]))) {
            tprintk("BAD DMA mapping\n");
            for(j=0; j<i; j++){
                if(j==0) dma_unmap_page(&pnvme_dev -> pdev-> dev, page_private(pages[j]), start_size, dir);
                else dma_unmap_page(&pnvme_dev -> pdev-> dev, page_private(pages[j]), PAGE_SIZE, dir);
            }
            release_pinned(pages, npages);
            kfree(pages);
            return -EINVAL;
	    }
        len_rem -= page_len;
        fp_offset = 0;

    }

    pcmd->rw.dptr.prp1 = cpu_to_le64(pages[0]-> private);
    if(npages == 2){ 
        pcmd->rw.dptr.prp2 = cpu_to_le64(pages[1]-> private); 
    }
    else if(npages > 2){
        tprintk("Create 1st PRP List\n");
        prp_list = dma_pool_alloc(pnvme_dev->prp_page_pool, GFP_KERNEL, &prp_dma);
        list[0] = prp_list;
        pcmd->rw.dptr.prp2 = cpu_to_le64(prp_dma);
        list_max = PAGE_SIZE/sizeof(__le64) -1;
        npages_list = npages -1;
        i=0;
        j=1;
        k=1;
        while(npages_list--){
            if(i == list_max){
                tprintk("Create %d PRP List\n", k+1);
                old_prp_list = prp_list;
                prp_list = dma_pool_alloc(pnvme_dev->prp_page_pool, GFP_KERNEL, &prp_dma);
                old_prp_list[i] =  cpu_to_le64(prp_dma);
                list[k++] = prp_list;
                i = 0;
            }
            prp_list[i] = cpu_to_le64(pages[j]->private);
            i++;
            j++;
        }
    }

    submitCmd(pcmd, pnvme_dev->pioQ[pnvme_dev->io_qid]);

    val = wait_event_interruptible_timeout(pnvme_dev->sdma_q, pnvme_dev->wait_condition == 1, TIMEOUT); //割り込み処理が完了してwait解除されるまで待ち

    for (i = 0; i < k; i++) {
        tprintk("Free DMA Pool\n");
		__le64 *prp_list = list[i];
		dma_addr_t next_prp_dma = le64_to_cpu(prp_list[list_max]);
		dma_pool_free(pnvme_dev->prp_page_pool, prp_list, prp_dma);
		prp_dma = next_prp_dma;
	}

    for(i=0; i<npages; i++){
        if(i==0) dma_unmap_page(&pnvme_dev -> pdev-> dev, page_private(pages[i]), start_size, dir);
        else dma_unmap_page(&pnvme_dev -> pdev-> dev, page_private(pages[i]), PAGE_SIZE, dir);
    }

map_error_handling:

    release_pinned(pages, npages);
    kfree(pages);
    return 0;

}

static long nvme_ioctl(struct file *filp, unsigned int ioctlnum, unsigned long ioctlparam){
    int rc;
    struct nvme_command cmd = {0};
    struct nvme_dev *pnvme_dev = filp->private_data;

    memset(pnvme_dev -> prp1_virt_addr, 0xaa, 512);

    //tprintk("Recieve ioctl\n");
    switch(ioctlnum){
        case IOCTL_IO_CMD:
            if((rc = copy_from_user(&cmd, (void  __user*)ioctlparam, sizeof(struct nvme_command)))){
                return rc;
            }

            if((rc = copy_from_user(pnvme_dev->prp1_virt_addr, (void  __user*)cmd.rw.dptr.prp1, 512))){
                return rc;
            }
            
            tprintk("ioctl data[0] %x\n", *(unsigned char*)pnvme_dev->prp1_virt_addr);

            cmd.rw.dptr.prp1 = cpu_to_le64(pnvme_dev -> prp1_dma_addr); 
            submitCmd(&cmd, pnvme_dev->pioQ[pnvme_dev->io_qid]);

            if(++pnvme_dev->io_qid == NUM_OF_IO_QUEUE_PAIR)
                pnvme_dev->io_qid = 0;

            break;
        
        case IOCTL_ADMIN_CMD:
            if((rc = copy_from_user(&cmd, (void  __user*)ioctlparam, sizeof(struct nvme_command)))){
                return rc;
            }
            cmd.rw.dptr.prp1 = cpu_to_le64(pnvme_dev ->prp1_dma_addr); 
            submitCmd(&cmd, pnvme_dev->padminQ);
            break;

        case IOCTL_MAP_PAGE:

            if((rc = copy_from_user(&cmd, (void  __user*)ioctlparam, sizeof(struct nvme_command)))){
                return rc;
            }

            set_prps(&cmd, pnvme_dev);
            break;

        default:
            break;
    }

    return 0;
}

static const struct file_operations nvme_chr_fops = {
    .owner = THIS_MODULE,
    .open = nvmet_pci_open,
    .release = nvmet_pci_close,
    .unlocked_ioctl = nvme_ioctl,
};

//割り込みハンドラ
static irqreturn_t nvme_irq(int irq, void *data)
{
    struct nvme_queue *nvmeq = data;
    struct nvme_completion cqe;
    u16 head, phase;
    head = nvmeq -> cq_head;     
    phase = nvmeq -> cq_phase;   
    rmb();

    while(1){
        cqe = nvmeq->cqes[head];  
     
        // phase tagが期待値と一致しない場合は、loopを抜ける。 phase tagはstatusフィールドの最初のbitらしい。
        if((le16_to_cpu(cqe.status) & 1) != phase){
            break;  
        }

        tprintk("Command id: 0x%x,  Status: 0x%x, phase tag %d, data[0] %x\n", le16_to_cpu(cqe.command_id), (le16_to_cpu(cqe.status) & 0xfe) >> 1, le16_to_cpu(cqe.status) & 1, *(unsigned char*)nvmeq->pnvme_dev->prp1_virt_addr);    

        nvmeq->sq_head = le16_to_cpu(cqe.sq_head);
        if(++head == nvmeq -> q_depth){
            head = 0;          //Completion Queueを一周したので、headを先頭に戻す。
            phase = !phase;    //Completion Queueを一周したので、phase tagを反転する。
        }
   }

    nvmeq->pnvme_dev->wait_condition = 1;
    wake_up_interruptible(&nvmeq->pnvme_dev->sdma_q);    

    if (head == nvmeq->cq_head && phase == nvmeq->cq_phase)
		return IRQ_NONE;

	writel(head, nvmeq -> pnvme_dev-> bar + NVME_REG_DBS + 4 + 8 * nvmeq->qid);  // Admin CQのドアベルレジスタは、0x1004にある。
	nvmeq->cq_head = head;
	nvmeq->cq_phase = phase;
    wmb();

    return  IRQ_HANDLED;
}


static int nvmet_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id){
    u32 ctrl_config = 0;
    int alloc_ret = 0;
    int cdev_err = 0;
    short vendor_id, device_id, class, sub_vendor_id, sub_device_id;
    u32 aqa;
    u32 csts;
    unsigned long timeout;
    int ret = -ENOMEM;
    int major, minor;
    int result;
    int index;
    int bad_index;

    struct nvme_dev *pnvme_dev;
    struct nvme_queue *padminQ;       // Admin Queue管理構造体へのポインタ  
    struct nvme_queue *pioQ[NUM_OF_IO_QUEUE_PAIR];          // IO Queue管理構造体へのポインタを一つだけ作成する。

    // Allocate device structure
    pnvme_dev = kmalloc(sizeof(struct nvme_dev), GFP_KERNEL);
    if(!pnvme_dev){
        goto out_alloc_pnvme_dev;
    }

    pnvme_dev->io_qid = 0;

    ret = ida_simple_get(&nvme_instance_ida, 0, 0, GFP_KERNEL);
    if (ret < 0){
        goto out_ida_simple_get;
    }
		
    pnvme_dev -> instance = ret;

    pnvme_dev -> devt = MKDEV(MAJOR(nvme_chr_devt), pnvme_dev -> instance);

    cdev_init(&pnvme_dev->cdev, &nvme_chr_fops);
    pnvme_dev->cdev.owner = THIS_MODULE;

    if ((ret = cdev_add(&pnvme_dev->cdev, pnvme_dev -> devt , NVME_MINORS)) != 0) {
        goto out_cdev_add;
    }

    device_create(nvme_class, NULL, MKDEV(MAJOR(nvme_chr_devt), pnvme_dev -> instance), NULL, "nvmet%d", pnvme_dev -> instance);
    
    // Device has only Memory mapped space, so pci_enable_device_mem is used.
    if (pci_enable_device_mem(pdev)){
        ret = -ENOMEM; 
        goto out_pci_enable_device;
    }
		
    pci_set_master(pdev);

    if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)) && dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32))){
        ret = -ENOMEM;
        goto out_dma_set_mask_and_coherent;
    }

    // Allocate admin Q
    padminQ = kmalloc(sizeof(struct nvme_queue), GFP_KERNEL);
    if(!padminQ){
        ret = -ENOMEM;
        goto out_alloc_adminQ;
    }
    pnvme_dev->padminQ = padminQ;
    padminQ->pnvme_dev = pnvme_dev;

    // Allocate IO Q
    for(index = 0; index < NUM_OF_IO_QUEUE_PAIR; ++index){
        pioQ[index] = kmalloc(sizeof(struct nvme_queue), GFP_KERNEL);
        if(!pioQ[index]){
            ret = -ENOMEM;
            goto out_alloc_pioQ;
        }
            pnvme_dev->pioQ[index] = pioQ[index];
            pioQ[index]->pnvme_dev = pnvme_dev;
    }

    ret = pci_request_regions(pdev, DEVICE_NAME);

    if(ret) {
        goto out_pci_request_regions;
    }

    pnvme_dev->pdev = pci_dev_get(pdev);
	pci_set_drvdata(pdev, pnvme_dev);

    //pnvme_dev->pdev = pdev;

    pnvme_dev->mmio_base = pci_resource_start(pdev, 0);
    pnvme_dev->mmio_length = pci_resource_len(pdev, 0);
    pnvme_dev->mmio_flags = pci_resource_flags(pdev, 0);
    tprintk( "mmio_base: 0x%llx, mmio_length: 0x%llx, mmio_flags: 0x%llx\n",pnvme_dev->mmio_base, pnvme_dev->mmio_length, pnvme_dev->mmio_flags);

    //Kernel Spaceの仮想アドレスにマッピングする。
    pnvme_dev->bar = ioremap(pnvme_dev->mmio_base, pnvme_dev->mmio_length);

    // Admin CQとIO CQ用に割り込みベクターは(NUM_OF_IO_QUEUE_PAIR + 1)個確保する。
    int nr_io_queues = pci_alloc_irq_vectors(pdev, 1, NUM_OF_IO_QUEUE_PAIR + 1, PCI_IRQ_ALL_TYPES | PCI_IRQ_AFFINITY);
    //int	error = pci_enable_msi(pdev);

    if (nr_io_queues != (NUM_OF_IO_QUEUE_PAIR + 1)) {
        ret = -ENOMEM;
        goto out_pci_alloc_irq_vectors;
	}

    tprintk( "nr io queues: %d\n", nr_io_queues);

    if (readl(pnvme_dev->bar + NVME_REG_VS) == NVME_VS(1, 2, 0)){
        tprintk( "NVMe version : 1.2\n");
    }else if(readl(pnvme_dev->bar + NVME_REG_VS) == NVME_VS(1, 3, 0)){
        tprintk( "NVMe version : 1.3\n");
    }else{
        tprintk( "NVMe version :unknown \n");
    }

    u32 cap = lo_hi_readq(pnvme_dev->bar + NVME_REG_CAP);
    tprintk("Doorbell Stride :%d\n", NVME_CAP_STRIDE(cap));

    // disable device
    ctrl_config = 0;
    ctrl_config &= ~NVME_CC_SHN_MASK;
	ctrl_config &= ~NVME_CC_ENABLE;
    writel(ctrl_config, pnvme_dev-> bar + NVME_REG_CC);

    timeout = ((NVME_CAP_TIMEOUT(cap) + 1) * HZ / 2) + jiffies;

    //Controller not ready になるまで待つ 
    while((readl(pnvme_dev->bar + NVME_REG_CSTS) & NVME_CSTS_RDY) != 0){
        tprintk("now ready. waiting...\n");
        msleep(100);
        if (time_after(jiffies, timeout)) {
            ret = -ENODEV;
            goto out_wait_not_ready_timeout;
		}
    }

    csts = readl(pnvme_dev->bar + NVME_REG_CSTS);
    tprintk("csts rdy : %d\n", csts & NVME_CSTS_RDY);

    // Admin CQを作成する
    padminQ->cqes = dma_alloc_coherent(&pdev->dev, CQ_SIZE(QUEUE_DEPTH), &padminQ->cq_dma_addr, GFP_KERNEL);
    if(padminQ->cqes == NULL){
         ret = -ENOMEM;
        goto out_alloc_adminCQ;
    }

    // Admin SQを作成する
    padminQ->sq_cmds = dma_alloc_coherent(&pdev->dev, SQ_SIZE(QUEUE_DEPTH), &padminQ->sq_dma_addr, GFP_KERNEL);
    if(padminQ->sq_cmds == NULL){
        ret = -ENOMEM;
        goto out_alloc_adminSQ;
    }

    //Admin Queue管理構造体の変数初期化
    spin_lock_init(&padminQ->q_lock);
    padminQ->sq_tail = 0;
    padminQ->cq_head = 0;
	padminQ->cq_phase = 1;    //デフォルトのphase tagは１。
    padminQ->q_depth = QUEUE_DEPTH;
	padminQ->qid = 0;
	padminQ->cq_vector = padminQ->qid;
    memset((void *)padminQ->cqes, 0, CQ_SIZE(QUEUE_DEPTH)); 

    //  コントローラのレジスタに設定する。Admin Queueのサイズは32固定とする。
    aqa = padminQ->q_depth - 1;
    aqa |= aqa << 16;
    writel(aqa, pnvme_dev-> bar + NVME_REG_AQA);
    lo_hi_writeq(padminQ-> sq_dma_addr, pnvme_dev->bar + NVME_REG_ASQ);
	lo_hi_writeq(padminQ-> cq_dma_addr, pnvme_dev->bar + NVME_REG_ACQ);

    unsigned dev_page_min = NVME_CAP_MPSMIN(cap) + 12;
    tprintk( "Minimum device page size %u\n", 1 << dev_page_min);

    // IO CQとSQを作成する
    for(index = 0; index < NUM_OF_IO_QUEUE_PAIR; ++index){
        pioQ[index]->cqes = dma_alloc_coherent(&pdev->dev, CQ_SIZE(QUEUE_DEPTH), &pioQ[index]->cq_dma_addr, GFP_KERNEL);

        if(pioQ[index]->cqes == NULL){
            ret = -ENOMEM;
            bad_index = index;
            goto out_alloc_ioCQ;
        }

        pioQ[index]->sq_cmds = dma_alloc_coherent(&pdev->dev, SQ_SIZE(QUEUE_DEPTH), &pioQ[index]->sq_dma_addr, GFP_KERNEL);

        if(pioQ[index]->sq_cmds == NULL){
            ret = -ENOMEM;
            bad_index = index;
            goto out_alloc_ioSQ;
        }

        //IO Queue管理構造体の変数初期化
        spin_lock_init(&pioQ[index]->q_lock);
        pioQ[index]->sq_tail = 0;
        pioQ[index]->cq_head = 0;
        pioQ[index]->cq_phase = 1;    //デフォルトのphase tagは１です。
        pioQ[index]->q_depth = QUEUE_DEPTH;
        pioQ[index]->qid = index + 1;
        pioQ[index]->cq_vector = pioQ[index]->qid;
        memset((void *)pioQ[index]->cqes, 0, CQ_SIZE(QUEUE_DEPTH));
    }

#if 0
    //disable device
    ctrl_config = (readl(pnvme_dev->bar + NVME_REG_CC) & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL;
    writel(ctrl_config, pnvme_dev->bar + NVME_REG_CC);
    timeout = ((NVME_CAP_TIMEOUT(cap) + 1) * HZ / 2) + jiffies;
    while ((readl(pnvme_dev->bar + NVME_REG_CSTS) & NVME_CSTS_RDY) == 1) {
        tprintk("Polling until not ready\n");
		msleep(100);
		if (time_after(jiffies, timeout)) {
			dev_err(&pnvme_dev->pdev->dev,
				"Device shutdownn incomplete; abort shutdown\n");
			return -ENODEV;
		}
	}
#endif
    //enable device
    unsigned page_shift = 12;
    ctrl_config = NVME_CC_CSS_NVM;
	ctrl_config |= (page_shift - 12) << NVME_CC_MPS_SHIFT;
	ctrl_config |= NVME_CC_AMS_RR | NVME_CC_SHN_NONE;
	ctrl_config |= NVME_CC_IOSQES | NVME_CC_IOCQES;
    //ctrl_config &= ~NVME_CC_SHN_MASK;
	ctrl_config |= NVME_CC_ENABLE;

    writel(ctrl_config, pnvme_dev-> bar + NVME_REG_CC);
    timeout = ((NVME_CAP_TIMEOUT(cap) + 1) * HZ / 2) + jiffies;

    //ready になるまで待つ 
    while((readl(pnvme_dev->bar + NVME_REG_CSTS) & NVME_CSTS_RDY) != 1){
        tprintk("Polling until ready\n");
        msleep(100);
        if (time_after(jiffies, timeout)) {
            ret = -ENODEV;
            goto out_wait_ready_timeout;
		}
    }

    //csts = 1になっていることをDebug print
    tprintk("csts rdy : %d\n", readl(pnvme_dev->bar + NVME_REG_CSTS) & NVME_CSTS_RDY);

    // identifyコマンドを確認するために作成したお試しData Buffer
    pnvme_dev ->prp1_virt_addr = dma_alloc_coherent(&pdev->dev, sizeof(struct nvme_id_ctrl), &pnvme_dev->prp1_dma_addr, GFP_KERNEL);
    if(pnvme_dev ->prp1_virt_addr == NULL){
        ret = -ENOMEM;
        goto out_alloc_data_buffer;
    }

    // prp listの作成
    pnvme_dev->prp_page_pool = dma_pool_create("prp list", &pnvme_dev->pdev->dev, PAGE_SIZE, PAGE_SIZE, 0);
    if (!pnvme_dev->prp_page_pool) {
		ret = -ENOMEM;
        goto out_create_dma_pool;
	}

    //pnvme_dev->prp_page_pool_later = dma_pool_create("prp list second", &pnvme_dev->pdev->dev, PAGE_SIZE, PAGE_SIZE, 0);
    //if (!pnvme_dev->prp_page_pool_first) {
//		ret = -ENOMEM;
  //      goto out_create_dma_pool_later;
	//}

    //割り込みハンドラーの設定 Admin CQ
    ret = pci_request_irq(pdev, padminQ->cq_vector, nvme_irq, NULL, padminQ, "nvme%dadminq%d", pnvme_dev->instance, padminQ->qid);
    if(ret < 0){
        goto out_pci_request_irq_admin;
    }

    // wait queueの初期化
    init_waitqueue_head(&pnvme_dev->sdma_q);

  	struct nvme_command cmd = {0};

    // Create IO CQコマンドを発行する。
    for(index = 0; index < NUM_OF_IO_QUEUE_PAIR; ++index){
        int flags = NVME_QUEUE_PHYS_CONTIG | NVME_CQ_IRQ_ENABLED;
        memset(&cmd, 0, sizeof(cmd));
        cmd.create_cq.opcode = nvme_admin_create_cq;
        cmd.create_cq.prp1 = cpu_to_le64(pioQ[index]->cq_dma_addr);
        cmd.create_cq.cqid = cpu_to_le16(pioQ[index]->qid);
        cmd.create_cq.qsize = cpu_to_le16(pioQ[index]->q_depth - 1);
        cmd.create_cq.cq_flags = cpu_to_le16(flags);
        cmd.create_cq.irq_vector = cpu_to_le16(pioQ[index]->cq_vector);
        cmd.create_cq.command_id = 0;
        submitCmd(&cmd, padminQ);

        // Create IO SQコマンドを発行する。
        flags = NVME_QUEUE_PHYS_CONTIG;

        memset(&cmd, 0, sizeof(cmd));
        cmd.create_sq.opcode = nvme_admin_create_sq;
        cmd.create_sq.prp1 = cpu_to_le64(pioQ[index]->sq_dma_addr);
        cmd.create_sq.sqid = cpu_to_le16(pioQ[index]->qid);
        cmd.create_sq.qsize = cpu_to_le16(pioQ[index]->q_depth - 1);
        cmd.create_sq.sq_flags = cpu_to_le16(flags);
        cmd.create_sq.cqid = cpu_to_le16(pioQ[index]->qid);
        cmd.create_sq.command_id = 1;
        submitCmd(&cmd, padminQ);
    
        msleep(100);

        //割り込みハンドラーの設定 IOCQ
        ret = pci_request_irq(pdev, pioQ[index]->cq_vector, nvme_irq, NULL, pioQ[index], "nvme%dioq%d", pnvme_dev->instance, pioQ[index]->qid);
        if(ret < 0){
            bad_index = index;
            goto out_pci_request_irq_io;
        }
    }

    return 0;

out_pci_request_irq_io:
    for(index = 0; index <= bad_index; ++index){
        //Delete SQコマンドを発行する
        memset(&cmd, 0, sizeof(cmd));
        cmd.delete_queue.opcode = nvme_admin_delete_sq;
	    cmd.delete_queue.qid = cpu_to_le16(pnvme_dev->pioQ[index]->qid);
        cmd.delete_queue.command_id = 5;
        submitCmd(&cmd, pnvme_dev->padminQ);

        //Delete CQコマンドを発行する
        memset(&cmd, 0, sizeof(cmd));
        cmd.delete_queue.opcode = nvme_admin_delete_cq;
	    cmd.delete_queue.qid = cpu_to_le16(pnvme_dev->pioQ[index]->qid);
        cmd.delete_queue.command_id = 6;  
        submitCmd(&cmd, pnvme_dev->padminQ);

    }

    for(index = 0; index < bad_index; ++index){
        pci_free_irq(pdev, padminQ->cq_vector, pnvme_dev->pioQ[index]);
    }

    pci_free_irq(pdev, padminQ->cq_vector, pnvme_dev->padminQ);

out_pci_request_irq_admin:
    dma_pool_destroy(pnvme_dev->prp_page_pool);   

//out_create_dma_pool_later:
 //   dma_pool_destroy(pnvme_dev->prp_page_pool_first);

out_create_dma_pool:
    dma_free_coherent(&pnvme_dev->pdev->dev, sizeof(struct nvme_id_ctrl), (void *)pnvme_dev->prp1_virt_addr, pnvme_dev ->prp1_dma_addr);

out_wait_ready_timeout:
out_alloc_data_buffer:
    for(index = 0; index < bad_index; ++index){
        dma_free_coherent(&pdev->dev, SQ_SIZE(QUEUE_DEPTH), (void *)pioQ[index]->sq_cmds, pioQ[index]->sq_dma_addr);
    }

out_alloc_ioSQ:
    for(index = 0; index < bad_index; ++index){
        dma_free_coherent(&pdev->dev, CQ_SIZE(QUEUE_DEPTH), (void *)pioQ[index]->cqes, pioQ[index]->cq_dma_addr);
    }

out_alloc_ioCQ:
    dma_free_coherent(&pdev->dev, SQ_SIZE(QUEUE_DEPTH), (void *)padminQ->sq_cmds, padminQ->sq_dma_addr);

out_alloc_adminSQ:
    dma_free_coherent(&pdev->dev, CQ_SIZE(QUEUE_DEPTH), (void *)padminQ->cqes, padminQ->cq_dma_addr);

out_wait_not_ready_timeout:
out_alloc_adminCQ:
    pci_free_irq_vectors(pdev);

out_pci_alloc_irq_vectors:
	iounmap(pnvme_dev->bar);

out_ioremap:
    pci_set_drvdata(pdev, NULL);

out_pci_set_drvdata:
	pci_release_regions(pdev);

out_pci_request_regions:
    kfree(pioQ);

out_alloc_pioQ:
    kfree(padminQ);

out_dma_set_mask_and_coherent:
out_alloc_adminQ:
    pci_disable_device(pdev);

out_pci_enable_device:
    device_destroy(nvme_class, pnvme_dev -> devt );

out_cdev_add:
    cdev_del(&pnvme_dev->cdev); 
    ida_simple_remove(&nvme_instance_ida, pnvme_dev -> instance );

out_ida_simple_get:
    kfree(pnvme_dev);

out_alloc_pnvme_dev:

    return ret;
}

static void nvmet_pci_remove(struct pci_dev* pdev){
    tprintk("Driver is removed.\n");
    struct nvme_command cmd = {0};
    struct nvme_dev *pnvme_dev = pci_get_drvdata(pdev);

    int index = 0;
    for(index = 0; index < NUM_OF_IO_QUEUE_PAIR; ++index){
        pci_free_irq(pdev, pnvme_dev->pioQ[index]->cq_vector, pnvme_dev ->pioQ[index]);
    
        //Delete SQコマンドを発行する
        memset(&cmd, 0, sizeof(cmd));
        cmd.delete_queue.opcode = nvme_admin_delete_sq;
	    cmd.delete_queue.qid = cpu_to_le16(pnvme_dev->pioQ[index]->qid);
        cmd.delete_queue.command_id = 5;
        submitCmd(&cmd, pnvme_dev->padminQ);

        //Delete CQコマンドを発行する
        memset(&cmd, 0, sizeof(cmd));
        cmd.delete_queue.opcode = nvme_admin_delete_cq;
	    cmd.delete_queue.qid = cpu_to_le16(pnvme_dev->pioQ[index]->qid);
        cmd.delete_queue.command_id = 6;  
        submitCmd(&cmd, pnvme_dev->padminQ);
    }
    
    pci_free_irq(pdev, pnvme_dev->padminQ->cq_vector, pnvme_dev ->padminQ);

    dma_pool_destroy(pnvme_dev->prp_page_pool);   
   // dma_pool_destroy(pnvme_dev->prp_page_pool_first);

    dma_free_coherent(&pnvme_dev->pdev->dev, sizeof(struct nvme_id_ctrl), (void *)pnvme_dev->prp1_virt_addr, pnvme_dev ->prp1_dma_addr);

    for(index = 0; index < NUM_OF_IO_QUEUE_PAIR; ++index){
        dma_free_coherent(&pnvme_dev->pdev->dev, SQ_SIZE(QUEUE_DEPTH), (void *)pnvme_dev->pioQ[index]->sq_cmds, pnvme_dev ->pioQ[index]->sq_dma_addr);
        dma_free_coherent(&pnvme_dev->pdev->dev, CQ_SIZE(QUEUE_DEPTH), (void *)pnvme_dev->pioQ[index]->cqes, pnvme_dev ->pioQ[index]->cq_dma_addr);
    }

    dma_free_coherent(&pnvme_dev->pdev->dev, SQ_SIZE(QUEUE_DEPTH), (void *)pnvme_dev->padminQ->sq_cmds, pnvme_dev ->padminQ->sq_dma_addr);
    dma_free_coherent(&pnvme_dev->pdev->dev, CQ_SIZE(QUEUE_DEPTH), (void *)pnvme_dev->padminQ->cqes, pnvme_dev ->padminQ->cq_dma_addr);
    pci_free_irq_vectors(pdev);
	iounmap(pnvme_dev->bar);
    pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);

    for(index = 0; index < NUM_OF_IO_QUEUE_PAIR; ++index){
        kfree(pnvme_dev->pioQ[index]);
    }

    kfree(pnvme_dev->padminQ);
    pci_disable_device(pdev);
    cdev_del(&pnvme_dev->cdev); 
    device_destroy(nvme_class, pnvme_dev->devt );
    ida_simple_remove(&nvme_instance_ida, pnvme_dev->instance );
    kfree(pnvme_dev);

}


// PCをシャットダウンしたときに呼ばれる
static void nvme_shutdown(struct pci_dev *pdev)
{
    unsigned long timeout;
    struct nvme_dev *pnvme_dev = pci_get_drvdata(pdev);
    u32 cc = (readl(pnvme_dev->bar + NVME_REG_CC) & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL;
    writel(cc, pnvme_dev->bar + NVME_REG_CC);
    timeout = 2 * HZ + jiffies;
    while ((readl(pnvme_dev->bar + NVME_REG_CSTS) & NVME_CSTS_SHST_MASK) != NVME_CSTS_SHST_CMPLT) {
		msleep(100);

		if (time_after(jiffies, timeout)) {
			dev_err(&pnvme_dev->pdev->dev,
				"Device shutdown incomplete; abort shutdown\n");
			//			return -ENODEV;
			return;
		}
	}
}

static struct pci_driver simple_nvme_driver = {
    .name = DEVICE_NAME,
    .id_table = test_nvme_ids,
    .probe = nvmet_pci_probe,
    .remove = nvmet_pci_remove,
    .shutdown = nvme_shutdown,
};

/* インストール時に実行 */
static int nvmet_init(void) {
    int ret;

    ida_init(&nvme_instance_ida);

	ret = alloc_chrdev_region(&nvme_chr_devt, 0, NVME_MINORS, DEVICE_NAME);
	if (ret != 0){
        printk(KERN_ERR "%d: %s()   %d",__LINE__, __FUNCTION__, ret ); 
        return ret;
    }

	nvme_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(nvme_class)) {
		ret = PTR_ERR(nvme_class);
        unregister_chrdev_region(nvme_chr_devt, NVME_MINORS);
		return ret;
	}

    ret = pci_register_driver(&simple_nvme_driver); //ドライバー名、table, コールバック関数(probe, remove)を登録
    if(ret != 0){
        printk(KERN_ERR "%d: %s()   %d",__LINE__, __FUNCTION__, ret );     
        return ret;
    }

    //timer_setup(&timerl, timer_handler, 0);
    //timerl.expires = jiffies + TIMER_CNT;
    //add_timer(&timerl);

    return ret; 
}

/* アンインストール時に実行 */
static void nvmet_exit(void) {

    //del_timer(&timerl);

    pci_unregister_driver(&simple_nvme_driver);  //ドライバー名、table, コールバック関数(probe, remove)を削除  
    class_destroy(nvme_class);
    unregister_chrdev_region(nvme_chr_devt, NVME_MINORS);
    ida_destroy(&nvme_instance_ida);
}

module_init(nvmet_init);
module_exit(nvmet_exit);
