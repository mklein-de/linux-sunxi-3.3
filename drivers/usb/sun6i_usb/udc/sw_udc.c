/*
*************************************************************************************
*                         			      Linux
*					           USB Device Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: sw_udc_udc.c
*
* Author 		: javen
*
* Description 	: USB Device 控制器驱动
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	   2010-3-3            1.0          create this file
*
*************************************************************************************
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <mach/irqs.h>

#include  "sw_udc_config.h"
#include  "sw_udc_board.h"

#include  "sw_udc_debug.h"
#include  "sw_udc_dma.h"

//---------------------------------------------------------------
//  宏 定义
//---------------------------------------------------------------
#define DRIVER_DESC	    "SoftWinner USB Device Controller"
#define DRIVER_VERSION	"20080411"
#define DRIVER_AUTHOR	"SoftWinner USB Developer"

//---------------------------------------------------------------
//  全局变量 定义
//---------------------------------------------------------------
static const char		gadget_name[] = "sw_usb_udc";
static const char		driver_desc[] = DRIVER_DESC;

static struct sw_udc	*the_controller = NULL;
static u32              usbd_port_no = 0;
static sw_udc_io_t      g_sw_udc_io;
static u32 usb_connect = 0;
static u32 is_controller_alive = 0;
static u8 is_udc_enable = 0;   /* is udc enable by gadget? */
extern int device_insmod_delay;
extern int axp_usbvol(void);
extern int axp_usbcur(void);
atomic_t ep_num_dma_rx;
int write_fifo_flag = 0;

unsigned long long  vfs_write_start = 0;
unsigned long long vfs_write_end  = 0;

#ifdef CONFIG_USB_SW_SUN6I_USB0_OTG
static struct platform_device *g_udc_pdev = NULL;
extern atomic_t thread_suspend_flag;
#endif

int dma_working = 0;
atomic_t vfs_read_flag;
atomic_t vfs_write_flag;
unsigned int vfs_amount = 0;
loff_t vfs_file_offset = 0;

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

static u8 crq_bRequest = 0;
static u8 crq_wIndex = 0;
static const unsigned char TestPkt[54] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAA,
		                                 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xEE, 0xEE, 0xEE,
		                                 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
		                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xBF, 0xDF,
		                                 0xEF, 0xF7, 0xFB, 0xFD, 0xFC, 0x7E, 0xBF, 0xDF, 0xEF, 0xF7,
		                                 0xFB, 0xFD, 0x7E, 0x00};

//---------------------------------------------------------------
//  debug
//---------------------------------------------------------------

static int g_queue_debug = 0;
static int g_dma_debug = 0;
static int g_write_debug = 0;
static int g_read_debug = 0;
static int g_irq_debug = 0;
int g_msc_write_debug = 0;
int g_msc_read_debug = 0;


static ssize_t show_ed_test(struct device *dev, struct device_attribute *attr, char *buf)
{

	return sprintf(buf, "%u\n", g_queue_debug);
}

static ssize_t ed_test(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	u32 fifo = 0;

	 if(!strncmp(buf, "test_j_state", 12)){
		USBC_EnterMode_Test_J(g_sw_udc_io.usb_bsp_hdle);
		printk("test_mode:%s\n", "test_j_state");
	}else if(!strncmp(buf, "test_k_state", 12)){
		USBC_EnterMode_Test_K(g_sw_udc_io.usb_bsp_hdle);
		printk("test_mode:%s\n", "test_k_state");
	}else if(!strncmp(buf, "test_se0_nak", 12)){
		USBC_EnterMode_Test_SE0_NAK(g_sw_udc_io.usb_bsp_hdle);
		printk("test_mode:%s\n", "test_se0_nak");
	}else if(!strncmp(buf, "test_pack", 9)){
		printk("test_mode___:%s\n", "test_pack");
	    fifo = USBC_SelectFIFO(g_sw_udc_io.usb_bsp_hdle, 0);
		USBC_WritePacket(g_sw_udc_io.usb_bsp_hdle, fifo, 54, (u32 *)TestPkt);
	    USBC_EnterMode_TestPacket(g_sw_udc_io.usb_bsp_hdle);
	    USBC_Dev_WriteDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 0);
	}else if(!strncmp(buf, "disable_test_mode", 17)){
		printk("start disable_test_mode\n");
		USBC_EnterMode_Idle(g_sw_udc_io.usb_bsp_hdle);
	}else {
		DMSG_PANIC("ERR: test_mode Argment is invalid\n");
	}

	printk("end test\n");
	return count;

}
static DEVICE_ATTR(otg_ed_test, 0644, show_ed_test, ed_test);


static ssize_t show_udc_debug(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", g_queue_debug);
}

static ssize_t udc_queue_debug(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
    int debug = 0;


    sscanf(buf, "%d", &debug);
    g_queue_debug = debug;

	return count;
}
static DEVICE_ATTR(queue_debug, 0644, show_udc_debug, udc_queue_debug);

static ssize_t show_dma_debug(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", g_dma_debug);
}

static ssize_t udc_dma_debug(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
    int debug = 0;


    sscanf(buf, "%d", &debug);
    g_dma_debug = debug;

	return count;
}
static DEVICE_ATTR(dma_debug, 0644, show_dma_debug, udc_dma_debug);

static ssize_t show_write_debug(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", g_write_debug);
}

static ssize_t udc_write_debug(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
    int debug = 0;


    sscanf(buf, "%d", &debug);
    g_write_debug = debug;

	return count;
}
static DEVICE_ATTR(write_debug, 0644, show_write_debug, udc_write_debug);

static ssize_t show_read_debug(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", g_read_debug);
}

static ssize_t udc_read_debug(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
    int debug = 0;


    sscanf(buf, "%d", &debug);
    g_read_debug = debug;

	return count;
}
static DEVICE_ATTR(read_debug, 0644, show_read_debug, udc_read_debug);

static ssize_t show_irq_debug(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", g_irq_debug);
}

static ssize_t udc_irq_debug(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
    int debug = 0;


    sscanf(buf, "%d", &debug);
    g_irq_debug = debug;

	return count;
}
static DEVICE_ATTR(irq_debug, 0644, show_irq_debug, udc_irq_debug);

static ssize_t show_msc_write_debug(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", g_msc_write_debug);
}

static ssize_t udc_msc_write_debug(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
    int debug = 0;


    sscanf(buf, "%d", &debug);
    g_msc_write_debug = debug;

	return count;
}
static DEVICE_ATTR(msc_write_debug, 0644, show_msc_write_debug, udc_msc_write_debug);

static ssize_t show_msc_read_debug(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", g_msc_read_debug);
}

static ssize_t udc_msc_read_debug(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
    int debug = 0;


    sscanf(buf, "%d", &debug);
    g_msc_read_debug = debug;

	return count;
}
static DEVICE_ATTR(msc_read_debug, 0644, show_msc_read_debug, udc_msc_read_debug);

//---------------------------------------------------------------
//  函数 定义
//---------------------------------------------------------------

static void cfg_udc_command(enum sw_udc_cmd_e cmd);
static void cfg_vbus_draw(unsigned int ma);


static __u32 is_peripheral_active(void)
{
	return is_controller_alive;
}
/*满足DMA传输的条件如下:
 * 1、驱动支持DMA传输
 * 2、非ep0
 * 3、大于一个包
 */
#define  big_req(req, ep) ((req->req.length != req->req.actual) \
                            ? ((req->req.length - req->req.actual) > ep->ep.maxpacket) \
                            : (req->req.length > ep->ep.maxpacket))
#define  is_sw_udc_dma_capable(req, ep)		(is_udc_support_dma() \
                                            && big_req(req, ep) \
                                            && req->req.dma_flag \
                                            && ep->num \
                                            && sw_udc_dma_channel_available(ep))

#define is_buffer_mapped(req, ep) (is_sw_udc_dma_capable(req, ep) && (req->map_state != UN_MAPPED))

/* Maps the buffer to dma  */
static inline void sw_udc_map_dma_buffer(struct sw_udc_request *req, struct sw_udc *udc, struct sw_udc_ep *ep)
{
	if(!is_sw_udc_dma_capable(req, ep)){
	    DMSG_PANIC("err: need not to dma map\n");
        return;
    }

    req->map_state = UN_MAPPED;

    if (req->req.dma == DMA_ADDR_INVALID) {
        req->req.dma = dma_map_single(
                udc->controller,
                req->req.buf,
                req->req.length,
                (is_tx_ep(ep) ? DMA_TO_DEVICE : DMA_FROM_DEVICE));
        req->map_state = SW_UDC_USB_MAPPED;
    } else {
        dma_sync_single_for_device(udc->controller,
            req->req.dma,
            req->req.length,
            (is_tx_ep(ep) ? DMA_TO_DEVICE : DMA_FROM_DEVICE));
        req->map_state = PRE_MAPPED;
    }

    return;
}

/* Unmap the buffer from dma and maps it back to cpu */
static inline void sw_udc_unmap_dma_buffer(struct sw_udc_request *req, struct sw_udc *udc, struct sw_udc_ep *ep)
{
	if(!is_buffer_mapped(req, ep)){
	    //DMSG_PANIC("err: need not to dma ummap\n");
		return;
    }

	if (req->req.dma == DMA_ADDR_INVALID) {
		DMSG_PANIC("not unmapping a never mapped buffer\n");
		return;
	}

	if (req->map_state == SW_UDC_USB_MAPPED) {
		dma_unmap_single(udc->controller,
			req->req.dma,
			req->req.length,
			(is_tx_ep(ep) ? DMA_TO_DEVICE : DMA_FROM_DEVICE));

		req->req.dma = DMA_ADDR_INVALID;
	} else { /* PRE_MAPPED */
		dma_sync_single_for_cpu(udc->controller,
			req->req.dma,
			req->req.length,
			(is_tx_ep(ep) ? DMA_TO_DEVICE : DMA_FROM_DEVICE));
	}

	req->map_state = UN_MAPPED;

	return;
}


/*
**********************************************************
*    关USB模块中断
**********************************************************
*/
static void disable_irq_udc(struct sw_udc *dev)
{
//    disable_irq(dev->irq_no);
}

/*
**********************************************************
*    开USB模块中断
**********************************************************
*/
static void enable_irq_udc(struct sw_udc *dev)
{
//    enable_irq(dev->irq_no);
}

/*
*******************************************************************************
*                     sw_udc_udc_done
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_done(struct sw_udc_ep *ep, struct sw_udc_request *req, int status)
__releases(ep->dev->lock)
__acquires(ep->dev->lock)
{
	unsigned halted = ep->halted;

	if(g_queue_debug){
		//DMSG_INFO("d: (0x%p, %d, %d)\n\n\n", &(req->req), req->req.length, req->req.actual);
		DMSG_INFO("d: (%s, %p, %d, %d)\n\n\n", ep->ep.name, &(req->req), req->req.length, req->req.actual);
	}

	list_del_init(&req->queue);

	if (likely (req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	ep->halted = 1;
	spin_unlock(&ep->dev->lock);
	if(is_sw_udc_dma_capable(req, ep)){
	    sw_udc_unmap_dma_buffer(req, ep->dev, ep);
	}
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&ep->dev->lock);
	ep->halted = halted;

	if(g_queue_debug){
		printk("%s:%d: %s, %p\n", __func__, __LINE__, ep->ep.name, &(req->req));
	}
}

/*
*******************************************************************************
*                     sw_udc_nuke
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_nuke(struct sw_udc *udc, struct sw_udc_ep *ep, int status)
{
	/* Sanity check */
	if (&ep->queue == NULL)
		return;

	while (!list_empty (&ep->queue)) {
		struct sw_udc_request *req;
		req = list_entry (ep->queue.next, struct sw_udc_request,queue);
		DMSG_INFO("nuke: ep num is %d\n", ep->num);
		sw_udc_done(ep, req, status);
	}
}

/*
*******************************************************************************
*                     sw_udc_clear_ep_state
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static inline void sw_udc_clear_ep_state(struct sw_udc *dev)
{
	unsigned i = 0;

	/* hardware SET_{CONFIGURATION,INTERFACE} automagic resets endpoint
	 * fifos, and pending transactions mustn't be continued in any case.
	 */

	for (i = 1; i < SW_UDC_ENDPOINTS; i++){
		sw_udc_nuke(dev, &dev->ep[i], -ECONNABORTED);
	}
}

/*
*******************************************************************************
*                     sw_udc_fifo_count_out
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static inline int sw_udc_fifo_count_out(__hdle usb_bsp_hdle, __u8 ep_index)
{
    if(ep_index){
        return USBC_ReadLenFromFifo(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
    }else{
        return USBC_ReadLenFromFifo(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);
    }
}

/*
*******************************************************************************
*                     sw_udc_write_packet
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static inline int sw_udc_write_packet(int fifo,
		struct sw_udc_request *req,
		unsigned max)
{
	unsigned len = min(req->req.length - req->req.actual, max);
	u8 *buf = req->req.buf + req->req.actual;

	prefetch(buf);

	DMSG_DBG_UDC("W: req.actual(%d), req.length(%d), len(%d), total(%d)\n",
		         req->req.actual, req->req.length, len, req->req.actual + len);

	req->req.actual += len;

	udelay(5);
	USBC_WritePacket(g_sw_udc_io.usb_bsp_hdle, fifo, len, buf);

	return len;
}

/*
*******************************************************************************
*                     pio_write_fifo
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int pio_write_fifo(struct sw_udc_ep *ep, struct sw_udc_request *req)
{
	unsigned    count       = 0;
	int		    is_last     = 0;
	u32		    idx         = 0;
	int		    fifo_reg    = 0;
	__s32 		ret 		= 0;
	u8		old_ep_index 	= 0;

	idx = ep->bEndpointAddress & 0x7F;

	/* 填写数据 */

    /* select ep */
	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, idx);

    /* select fifo */
    fifo_reg = USBC_SelectFIFO(g_sw_udc_io.usb_bsp_hdle, idx);

	count = sw_udc_write_packet(fifo_reg, req, ep->ep.maxpacket);

	/* 判断最后一个数据包 */

	/* last packet is often short (sometimes a zlp) */
	if(count != ep->ep.maxpacket){
		is_last = 1;
	}else if (req->req.length != req->req.actual || req->req.zero){
		is_last = 0;
	}else{
	    is_last = 2;
	}

    if(g_write_debug){
	    DMSG_INFO("pw: (0x%p, %d, %d)\n", &(req->req), req->req.length, req->req.actual);
    }

	if(idx){  //ep1~4
		ret = USBC_Dev_WriteDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX, is_last);
		if(ret != 0){
			DMSG_PANIC("ERR: USBC_Dev_WriteDataStatus, failed\n");
		    req->req.status = -EOVERFLOW;
		}
	}else{  //ep0
		ret = USBC_Dev_WriteDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, is_last);
		if(ret != 0){
			DMSG_PANIC("ERR: USBC_Dev_WriteDataStatus, failed\n");
			req->req.status = -EOVERFLOW;
		}
	}

	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	if(is_last){
		if(!idx){  /* ep0 */
			ep->dev->ep0state=EP0_IDLE;
		}

		sw_udc_done(ep,req, 0);
		is_last = 1;
	}

	return is_last;
}

/*
*******************************************************************************
*                     dma_write_fifo
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int dma_write_fifo(struct sw_udc_ep *ep, struct sw_udc_request *req)
{
	u32   	left_len 	= 0;
	u32		idx         = 0;
	int		fifo_reg    = 0;
	u8		old_ep_index 	= 0;

	idx = ep->bEndpointAddress & 0x7F;

    /* select ep */
	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, idx);

    /* select fifo */
    fifo_reg = USBC_SelectFIFO(g_sw_udc_io.usb_bsp_hdle, idx);

	ep->dma->dma_working = 1;
	dma_working = 1;

    /* auto_set, tx_mode, dma_tx_en, mode1 */
	USBC_Dev_ConfigEpDma(ep->dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_TX);

	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	/* 截取非整包部分 */
	left_len = req->req.length - req->req.actual;
    left_len = left_len - (left_len % ep->ep.maxpacket);

    if(g_dma_debug){
	    DMSG_INFO("dw: (0x%p, %d, %d)\n", &(req->req), req->req.length, req->req.actual);
    }

    spin_unlock(&ep->dev->lock);
	sw_udc_dma_set_config(ep, req, (__u32)req->req.dma, left_len);
	sw_udc_dma_start(ep, fifo_reg, (__u32)req->req.dma, left_len);
	spin_lock(&ep->dev->lock);

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_write_fifo
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    0 = still running, 1 = completed, negative = errno
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_write_fifo(struct sw_udc_ep *ep, struct sw_udc_request *req)
{
	if(sw_udc_dma_is_busy(ep)){
		if(g_dma_debug){
		    struct sw_udc_request *req_next = NULL;

			DMSG_PANIC("ERR: dma is busy, write fifo. ep(0x%p, %d), req(0x%p, 0x%p, 0x%x, %d, %d)\n\n",
							ep, ep->num,
							req, &(req->req), (u32)req->req.buf, req->req.length, req->req.actual);

		    if(likely (!list_empty(&ep->queue))){
		        req_next = list_entry(ep->queue.next, struct sw_udc_request, queue);
		    }else{
		        req_next = NULL;
		    }

		    if(req_next){
		        DMSG_PANIC("ERR: dma is busy, write fifo. req(0x%p, 0x%p, 0x%x, %d, %d)\n\n",
		                        req_next, &(req_next->req), (u32)req_next->req.buf, req_next->req.length, req_next->req.actual);
		    }
		}

		return 0;
	}

	if(is_sw_udc_dma_capable(req, ep)){
		return dma_write_fifo(ep, req);
	}else{
		return pio_write_fifo(ep, req);
	}
}

/*
*******************************************************************************
*                     sw_udc_read_packet
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static inline int sw_udc_read_packet(int fifo, u8 *buf,
		struct sw_udc_request *req, unsigned avail)
{
	unsigned len = 0;

	len = min(req->req.length - req->req.actual, avail);
	req->req.actual += len;

	DMSG_DBG_UDC("R: req.actual(%d), req.length(%d), len(%d), total(%d)\n",
		         req->req.actual, req->req.length, len, req->req.actual + len);

	USBC_ReadPacket(g_sw_udc_io.usb_bsp_hdle, fifo, len, buf);

	return len;
}

/*
*******************************************************************************
*                     pio_read_fifo
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    0 = still running, 1 = completed, negative = errno
*
* note:
*    void
*
*******************************************************************************
*/
static int pio_read_fifo(struct sw_udc_ep *ep, struct sw_udc_request *req)
{
	u8			*buf 		= NULL;
	unsigned	bufferspace = 0;
	int			is_last		= 1;
	unsigned	avail 		= 0;
	int			fifo_count 	= 0;
	u32			idx 		= 0;
	int			fifo_reg 	= 0;
	__s32 		ret 		= 0;
	u8		old_ep_index 	= 0;

	idx = ep->bEndpointAddress & 0x7F;

    /* select fifo */
    fifo_reg = USBC_SelectFIFO(g_sw_udc_io.usb_bsp_hdle, idx);

	if(!req->req.length){
	    DMSG_PANIC("ERR: req->req.length == 0\n");
		return 1;
    }

	buf = req->req.buf + req->req.actual;
	bufferspace = req->req.length - req->req.actual;
	if (!bufferspace) {
		DMSG_PANIC("ERR: buffer full!\n");
		return -1;
	}

    /* select ep */
	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, idx);

	fifo_count = sw_udc_fifo_count_out(g_sw_udc_io.usb_bsp_hdle, idx);
	if(fifo_count > ep->ep.maxpacket){
		avail = ep->ep.maxpacket;
	}else{
		avail = fifo_count;
    }

	fifo_count = sw_udc_read_packet(fifo_reg, buf, req, avail);

	/* checking this with ep0 is not accurate as we already
	 * read a control request
	 **/
	if (idx != 0 && fifo_count < ep->ep.maxpacket) {
		is_last = 1;
		/* overflowed this request?  flush extra data */
		if (fifo_count != avail)
			req->req.status = -EOVERFLOW;
	} else {
		is_last = (req->req.length <= req->req.actual) ? 1 : 0;
	}

    if(g_read_debug){
	    DMSG_INFO("pr: (0x%p, %d, %d)\n", &(req->req), req->req.length, req->req.actual);
    }

    if (idx){
		ret = USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX, is_last);
		if(ret != 0){
			DMSG_PANIC("ERR: pio_read_fifo: USBC_Dev_WriteDataStatus, failed\n");
			req->req.status = -EOVERFLOW;
		}
    }else{
		ret = USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, is_last);
		if(ret != 0){
			DMSG_PANIC("ERR: pio_read_fifo: USBC_Dev_WriteDataStatus, failed\n");
			req->req.status = -EOVERFLOW;
		}
    }

	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	if(is_last){
		if(!idx){
			ep->dev->ep0state = EP0_IDLE;
		}

        sw_udc_done(ep, req, 0);
        is_last = 1;
	}

	return is_last;
}

/*
*******************************************************************************
*                     dma_read_fifo
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int dma_read_fifo(struct sw_udc_ep *ep, struct sw_udc_request *req)
{
	u32   	left_len 	= 0;
	u32		idx         = 0;
	int		fifo_reg    = 0;
	u8		old_ep_index 	= 0;

	idx = ep->bEndpointAddress & 0x7F;

    /* select ep */
	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, idx);
    /* select fifo */
    fifo_reg = USBC_SelectFIFO(g_sw_udc_io.usb_bsp_hdle, idx);

	ep->dma->dma_working = 1;
	dma_working = 1;

    /* auto_set, tx_mode, dma_tx_en, mode1 */
	USBC_Dev_ConfigEpDma(ep->dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_RX);

	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	/* 截取非整包部分 */
	left_len = req->req.length - req->req.actual;
    left_len = left_len - (left_len % ep->ep.maxpacket);

    if(g_dma_debug){
	    DMSG_INFO("dr: (0x%p, %d, %d)\n", &(req->req), req->req.length, req->req.actual);
    }

	spin_unlock(&ep->dev->lock);
	sw_udc_dma_set_config(ep, req, (__u32)req->req.dma, left_len);
	sw_udc_dma_start(ep, fifo_reg, (__u32)req->req.dma, left_len);
	spin_lock(&ep->dev->lock);

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_read_fifo
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    0 = still running, 1 = completed, negative = errno
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_read_fifo(struct sw_udc_ep *ep, struct sw_udc_request *req)
{
	u32			idx 		= 0;
	u8		old_ep_index 	= 0;
	int 		fifo_count	= 0;

	if(sw_udc_dma_is_busy(ep)){
		if(g_dma_debug){
		    struct sw_udc_request *req_next = NULL;

			DMSG_PANIC("ERR: dma is busy, read fifo. ep(0x%p, %d), req(0x%p, 0x%p, 0x%x, %d, %d)\n\n",
							ep, ep->num,
							req, &(req->req), (u32)req->req.buf, req->req.length, req->req.actual);

		    if(likely (!list_empty(&ep->queue))){
		        req_next = list_entry(ep->queue.next, struct sw_udc_request, queue);
		    }else{
		        req_next = NULL;
		    }

		    if(req_next){
				DMSG_PANIC("ERR: dma is busy, read fifo. req(0x%p, 0x%p, 0x%x, %d, %d)\n\n",
								req_next, &(req_next->req), (u32)req_next->req.buf, req_next->req.length, req_next->req.actual);
		    }
		}

		return 0;
	}


	idx = ep->bEndpointAddress & 0x7F;

    /* select ep */
	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, idx);

	fifo_count = sw_udc_fifo_count_out(g_sw_udc_io.usb_bsp_hdle, idx);

	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

       if(is_sw_udc_dma_capable(req, ep)){
		return dma_read_fifo(ep, req);
	}else{
		return pio_read_fifo(ep, req);
	}
}

/*
*******************************************************************************
*                     sw_udc_read_fifo_crq
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_read_fifo_crq(struct usb_ctrlrequest *crq)
{
	u32 fifo_count  = 0;
	u32 i           = 0;
    u8  *pOut       = (u8 *) crq;
	u32 fifo        = 0;

    fifo = USBC_SelectFIFO(g_sw_udc_io.usb_bsp_hdle, 0);
    fifo_count = USBC_ReadLenFromFifo(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);

	if(fifo_count != 8){
		i = 0;

		while(i < 16 && (fifo_count != 8) ){
			fifo_count = USBC_ReadLenFromFifo(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);
			i++;
		}

		if(i >= 16){
			DMSG_PANIC("ERR: get ep0 fifo len failed\n");
		}
	}

    return USBC_ReadPacket(g_sw_udc_io.usb_bsp_hdle, fifo, fifo_count, pOut);
}

/*
*******************************************************************************
*                     sw_udc_get_status
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_get_status(struct sw_udc *dev, struct usb_ctrlrequest *crq)
{
	u16 status  = 0;
	u8 	buf[8];
	u8  ep_num  = crq->wIndex & 0x7F;
	u8  is_in   = crq->wIndex & USB_DIR_IN;
	u32 fifo = 0;

	switch (crq->bRequestType & USB_RECIP_MASK) {
    	case USB_RECIP_INTERFACE:
			buf[0] = 0x00;
			buf[1] = 0x00;
    	break;

	    case USB_RECIP_DEVICE:
		    status = dev->devstatus;
			buf[0] = 0x01;
			buf[1] = 0x00;
		break;

    	case USB_RECIP_ENDPOINT:
    		if (ep_num > 4 || crq->wLength > 2){
    			return 1;
    		}

            USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, ep_num);
    		if (ep_num == 0) {
    			status = USBC_Dev_IsEpStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);
    		} else {
    			if (is_in) {
    				status = USBC_Dev_IsEpStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);
    			} else {
    				status = USBC_Dev_IsEpStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
    			}
    		}
    		status = status ? 1 : 0;
			if (status) {
				buf[0] = 0x01;
				buf[1] = 0x00;
			} else {
				buf[0] = 0x00;
				buf[1] = 0x00;
			}
		break;

    	default:
    	return 1;
	}

	/* Seems to be needed to get it working. ouch :( */
	udelay(5);
	USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 0);

	fifo = USBC_SelectFIFO(g_sw_udc_io.usb_bsp_hdle, 0);
	USBC_WritePacket(g_sw_udc_io.usb_bsp_hdle, fifo, crq->wLength, buf);
	USBC_Dev_WriteDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);

	return 0;
}

static int sw_udc_set_halt(struct usb_ep *_ep, int value);
static int sw_udc_set_halt_ex(struct usb_ep *_ep, int value);

/*
*******************************************************************************
*                     sw_udc_handle_ep0_idle
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_handle_ep0_idle(struct sw_udc *dev,
					struct sw_udc_ep *ep,
					struct usb_ctrlrequest *crq,
					u32 ep0csr)
{
	int len = 0, ret = 0, tmp = 0;

	/* start control request? */
	if (!USBC_Dev_IsReadDataReady(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0)){
	    DMSG_WRN("ERR: data is ready, can not read data.\n");
		return;
    }

	sw_udc_nuke(dev, ep, -EPROTO);

	len = sw_udc_read_fifo_crq(crq);
	if (len != sizeof(*crq)) {
		DMSG_PANIC("setup begin: fifo READ ERROR"
			" wanted %d bytes got %d. Stalling out...\n",
			sizeof(*crq), len);

		USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 0);
		USBC_Dev_EpSendStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);

		return;
	}

	DMSG_DBG_UDC("ep0: bRequest = %d bRequestType %d wLength = %d\n",
		crq->bRequest, crq->bRequestType, crq->wLength);

	/* cope with automagic for some standard requests. */
	dev->req_std        = ((crq->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD);
	dev->req_config     = 0;
	dev->req_pending    = 1;

    if(dev->req_std){   //standard request
    	switch (crq->bRequest) {
        	case USB_REQ_SET_CONFIGURATION:
        		DMSG_DBG_UDC("USB_REQ_SET_CONFIGURATION ... \n");

        		if (crq->bRequestType == USB_RECIP_DEVICE) {
        			dev->req_config = 1;
        		}
    		break;

        	case USB_REQ_SET_INTERFACE:
        		DMSG_DBG_UDC("USB_REQ_SET_INTERFACE ... \n");

        		if (crq->bRequestType == USB_RECIP_INTERFACE) {
        			dev->req_config = 1;
        		}
    		break;

        	case USB_REQ_SET_ADDRESS:
        		DMSG_DBG_UDC("USB_REQ_SET_ADDRESS ... \n");

        		if (crq->bRequestType == USB_RECIP_DEVICE) {
        			tmp = crq->wValue & 0x7F;
        			dev->address = tmp;

        			//rx接收完毕、dataend、tx_pakect准备就绪
    				USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);

    				dev->ep0state = EP0_END_XFER;

    				crq_bRequest = USB_REQ_SET_ADDRESS;

        			return;
        		}
    		break;

        	case USB_REQ_GET_STATUS:
        		DMSG_DBG_UDC("USB_REQ_GET_STATUS ... \n");

     			if (!sw_udc_get_status(dev, crq)) {
    				return;
    			}
    		break;

    		case USB_REQ_CLEAR_FEATURE:
    			//--<1>--数据方向必须为 host to device
    			if(x_test_bit(crq->bRequestType, 7)){
    				DMSG_PANIC("USB_REQ_CLEAR_FEATURE: data is not host to device\n");
    				break;
    			}

    			USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);

    			//--<3>--数据阶段
    			if(crq->bRequestType == USB_RECIP_DEVICE){
    				/* wValue 0-1 */
    				if(crq->wValue){
    					dev->devstatus &= ~(1 << USB_DEVICE_REMOTE_WAKEUP);
    				}else{
    					int k = 0;
    					for(k = 0;k < SW_UDC_ENDPOINTS;k++){
								sw_udc_set_halt_ex(&dev->ep[k].ep, 0);
    					}
    				}

    			}else if(crq->bRequestType == USB_RECIP_INTERFACE){
    				//--<2>--令牌阶段结束

    				//不处理

    			}else if(crq->bRequestType == USB_RECIP_ENDPOINT){
    				//--<3>--解除禁用ep
    				//sw_udc_set_halt(&dev->ep[crq->wIndex & 0x7f].ep, 0);
    				//dev->devstatus &= ~(1 << USB_DEVICE_REMOTE_WAKEUP);
    				/* wValue 0-1 */
    				if(crq->wValue){
    					dev->devstatus &= ~(1 << USB_DEVICE_REMOTE_WAKEUP);
    				}else{
							sw_udc_set_halt_ex(&dev->ep[crq->wIndex & 0x7f].ep, 0);
    				}

    			}else{
    				DMSG_PANIC("PANIC : nonsupport set feature request. (%d)\n", crq->bRequestType);
    				USBC_Dev_EpSendStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);
    			}

    			dev->ep0state = EP0_IDLE;

    			return;
    		//break;

            case USB_REQ_SET_FEATURE:
                //--<1>--数据方向必须为 host to device
                if(x_test_bit(crq->bRequestType, 7)){
                    DMSG_PANIC("USB_REQ_SET_FEATURE: data is not host to device\n");
                    break;
                }

                //--<3>--数据阶段
                if(crq->bRequestType == USB_RECIP_DEVICE){
                    if((crq->wValue == USB_DEVICE_TEST_MODE)){
                        //setup packet包接收完毕
						switch (crq->wIndex){
							case SW_UDC_TEST_J:
								USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
								dev->ep0state = EP0_END_XFER;
								crq_wIndex = TEST_J;
								crq_bRequest = USB_REQ_SET_FEATURE;
								return;
							case SW_UDC_TEST_K:
								USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
		                        dev->ep0state = EP0_END_XFER;
								crq_wIndex = TEST_K;
		                        crq_bRequest = USB_REQ_SET_FEATURE;
								return;
							case SW_UDC_TEST_SE0_NAK:
								USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
		                        dev->ep0state = EP0_END_XFER;
								crq_wIndex = TEST_SE0_NAK;
		                        crq_bRequest = USB_REQ_SET_FEATURE;
								return;
							case SW_UDC_TEST_PACKET:
								USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
		                        dev->ep0state = EP0_END_XFER;
								crq_wIndex = TEST_PACKET;
		                        crq_bRequest = USB_REQ_SET_FEATURE;
								return;
							default:
							break;
						}
                    }

    				USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
    				dev->devstatus |= (1 << USB_DEVICE_REMOTE_WAKEUP);
                }else if(crq->bRequestType == USB_RECIP_INTERFACE){
                    //--<2>--令牌阶段结束
                    USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
                    //不处理

                }else if(crq->bRequestType == USB_RECIP_ENDPOINT){
                    //--<3>--禁用ep
                    USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
										sw_udc_set_halt_ex(&dev->ep[crq->wIndex & 0x7f].ep, 1);
                }else{
                    DMSG_PANIC("PANIC : nonsupport set feature request. (%d)\n", crq->bRequestType);

                    USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
                    USBC_Dev_EpSendStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);
                }

                dev->ep0state = EP0_IDLE;

    			return;
            //break;

        	default:
    			/* 只收setup数据包，不能置DataEnd */
    			USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 0);
        		break;
    	}
    }else{
        USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 0);
    }

	if(crq->bRequestType & USB_DIR_IN){
		dev->ep0state = EP0_IN_DATA_PHASE;
	}else{
		dev->ep0state = EP0_OUT_DATA_PHASE;
    }

	if(!dev->driver)
		return;

	spin_unlock(&dev->lock);
	ret = dev->driver->setup(&dev->gadget, crq);
	spin_lock(&dev->lock);
	if (ret < 0) {
		if (dev->req_config) {
			DMSG_PANIC("ERR: config change %02x fail %d?\n", crq->bRequest, ret);
			return;
		}

		if(ret == -EOPNOTSUPP){
			DMSG_PANIC("ERR: Operation not supported\n");
		}else{
			DMSG_PANIC("ERR: dev->driver->setup failed. (%d)\n", ret);
        }

		udelay(5);

		USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
		USBC_Dev_EpSendStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);

		dev->ep0state = EP0_IDLE;
		/* deferred i/o == no response yet */
	} else if (dev->req_pending) {
//		DMSG_PANIC("ERR: dev->req_pending... what now?\n");
		dev->req_pending=0;
	}

	if(crq->bRequest == USB_REQ_SET_CONFIGURATION || crq->bRequest == USB_REQ_SET_INTERFACE){
		//rx_packet包接收完毕
		USBC_Dev_ReadDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 1);
	}

	return;
}

/*
*******************************************************************************
*                     FunctionName
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_handle_ep0(struct sw_udc *dev)
{
	u32			            ep0csr  = 0;
	struct sw_udc_ep	    *ep     = &dev->ep[0];
	struct sw_udc_request	*req    = NULL;
	struct usb_ctrlrequest	crq;

DMSG_DBG_UDC("sw_udc_handle_ep0--1--\n");

	if(list_empty(&ep->queue)){
		req = NULL;
	}else{
		req = list_entry(ep->queue.next, struct sw_udc_request, queue);
    }

DMSG_DBG_UDC("sw_udc_handle_ep0--2--\n");


	/* We make the assumption that sw_udc_UDC_IN_CSR1_REG equal to
	 * sw_udc_UDC_EP0_CSR_REG when index is zero */
    USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, 0);

	/* clear stall status */
	if (USBC_Dev_IsEpStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0)) {
		DMSG_PANIC("ERR: ep0 stall\n");

		sw_udc_nuke(dev, ep, -EPIPE);
		USBC_Dev_EpClearStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);
		dev->ep0state = EP0_IDLE;

		return;
	}

	/* clear setup end */
	if (USBC_Dev_Ctrl_IsSetupEnd(g_sw_udc_io.usb_bsp_hdle)) {
		DMSG_PANIC("handle_ep0: ep0 setup end\n");

		sw_udc_nuke(dev, ep, 0);
		USBC_Dev_Ctrl_ClearSetupEnd(g_sw_udc_io.usb_bsp_hdle);
		dev->ep0state = EP0_IDLE;
	}


DMSG_DBG_UDC("sw_udc_handle_ep0--3--%d\n", dev->ep0state);


	ep0csr = USBC_Readw(USBC_REG_CSR0(g_sw_udc_io.usb_vbase));

	switch (dev->ep0state) {
    	case EP0_IDLE:
    		sw_udc_handle_ep0_idle(dev, ep, &crq, ep0csr);
		break;

    	case EP0_IN_DATA_PHASE:			/* GET_DESCRIPTOR etc */
    		DMSG_DBG_UDC("EP0_IN_DATA_PHASE ... what now?\n");

    		if (!USBC_Dev_IsWriteDataReady(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0) && req) {
    			sw_udc_write_fifo(ep, req);
    		}
		break;

    	case EP0_OUT_DATA_PHASE:		/* SET_DESCRIPTOR etc */
    		DMSG_DBG_UDC("EP0_OUT_DATA_PHASE ... what now?\n");

    		if (USBC_Dev_IsReadDataReady(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0) && req ) {
    			sw_udc_read_fifo(ep,req);
    		}
		break;

    	case EP0_END_XFER:
    		DMSG_DBG_UDC("EP0_END_XFER ... what now?\n");
    		DMSG_DBG_UDC("crq_bRequest = 0x%x\n", crq_bRequest);

			switch (crq_bRequest){
		    	case USB_REQ_SET_ADDRESS:

#ifndef  SW_USB_FPGA
					axp_usbvol();
					axp_usbcur();
#endif
					USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, 0);

					USBC_Dev_Ctrl_ClearSetupEnd(g_sw_udc_io.usb_bsp_hdle);
					USBC_Dev_SetAddress(g_sw_udc_io.usb_bsp_hdle, dev->address);

					DMSG_INFO_UDC("Set address %d\n", dev->address);
				break;

                case USB_REQ_SET_FEATURE:
                {
					switch (crq_wIndex){
						case TEST_J:
							USBC_EnterMode_Test_J(g_sw_udc_io.usb_bsp_hdle);
							break;

						case TEST_K:
							USBC_EnterMode_Test_K(g_sw_udc_io.usb_bsp_hdle);
							break;

						case TEST_SE0_NAK:
							USBC_EnterMode_Test_SE0_NAK(g_sw_udc_io.usb_bsp_hdle);
							break;

						case TEST_PACKET:
						{
							u32 fifo = 0;
							fifo = USBC_SelectFIFO(g_sw_udc_io.usb_bsp_hdle, 0);
							USBC_WritePacket(g_sw_udc_io.usb_bsp_hdle, fifo, 54, (u32 *)TestPkt);
							USBC_EnterMode_TestPacket(g_sw_udc_io.usb_bsp_hdle);
							USBC_Dev_WriteDataStatus(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0, 0);
						}
							break;
						default:
						break;
					}

					crq_wIndex = 0;
                }
                break;

				default:
				break;
		    }

			crq_bRequest = 0;

    		dev->ep0state = EP0_IDLE;
		break;

    	case EP0_STALL:
    		DMSG_DBG_UDC("EP0_STALL ... what now?\n");
    		dev->ep0state = EP0_IDLE;
		break;
	}

DMSG_DBG_UDC("sw_udc_handle_ep0--4--%d\n", dev->ep0state);


	return;
}

/*
*******************************************************************************
*                     FunctionName
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_handle_ep(struct sw_udc_ep *ep)
{
	struct sw_udc_request	*req = NULL;
	int is_in   = ep->bEndpointAddress & USB_DIR_IN;
	u32 idx     = 0;
	u8 old_ep_index = 0;

	/* see sw_udc_queue. */
	if(likely (!list_empty(&ep->queue))){
		req = list_entry(ep->queue.next, struct sw_udc_request, queue);
	}else{
		req = NULL;
    }

    if(g_irq_debug){
	    DMSG_INFO("e: (%s), tx_csr=0x%x\n", ep->ep.name, USBC_Readw(USBC_REG_TXCSR(g_sw_udc_io.usb_vbase)));
		if(req){
		    DMSG_INFO("req: (0x%p, %d, %d)\n", &(req->req), req->req.length, req->req.actual);
		}
    }

	idx = ep->bEndpointAddress & 0x7F;

	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
    USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, idx);

	if (is_in) {
		if (USBC_Dev_IsEpStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX)) {
			DMSG_PANIC("ERR: tx ep(%d) is stall\n", idx);
			USBC_Dev_EpClearStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);
		    goto end;
		}
	} else {
		if (USBC_Dev_IsEpStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX)) {
			DMSG_PANIC("ERR: rx ep(%d) is stall\n", idx);
            USBC_Dev_EpClearStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
		    goto end;
		}
	}

	if(req){
	    if(is_in){
		    if(!USBC_Dev_IsWriteDataReady(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX)){
			    sw_udc_write_fifo(ep, req);
		    }
	    }else{
            if(USBC_Dev_IsReadDataReady(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX)){
                sw_udc_read_fifo(ep,req);
            }
	    }
	}

end:
	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	return;
}

/*
*******************************************************************************
*                     filtrate_irq_misc
*
* Description:
*    过滤没用的中断, 保留 disconect, reset, resume, suspend
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static u32 filtrate_irq_misc(u32 irq_misc)
{
    u32 irq = irq_misc;

    irq &= ~(USBC_INTUSB_VBUS_ERROR | USBC_INTUSB_SESSION_REQ | USBC_INTUSB_CONNECT | USBC_INTUSB_SOF);
	USBC_INT_ClearMiscPending(g_sw_udc_io.usb_bsp_hdle, USBC_INTUSB_VBUS_ERROR);
	USBC_INT_ClearMiscPending(g_sw_udc_io.usb_bsp_hdle, USBC_INTUSB_SESSION_REQ);
	USBC_INT_ClearMiscPending(g_sw_udc_io.usb_bsp_hdle, USBC_INTUSB_CONNECT);
	USBC_INT_ClearMiscPending(g_sw_udc_io.usb_bsp_hdle, USBC_INTUSB_SOF);

	return irq;
}

/*
*******************************************************************************
*                     clear_all_irq
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void clear_all_irq(void)
{
    USBC_INT_ClearEpPendingAll(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);
    USBC_INT_ClearEpPendingAll(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
    USBC_INT_ClearMiscPendingAll(g_sw_udc_io.usb_bsp_hdle);
}

/*
*******************************************************************************
*                     throw_away_all_urb
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void throw_away_all_urb(struct sw_udc *dev)
{
	int k = 0;

	DMSG_INFO_UDC("irq: reset happen, throw away all urb\n");
	for(k = 0; k < SW_UDC_ENDPOINTS; k++){
		sw_udc_nuke(dev, (struct sw_udc_ep * )&(dev->ep[k]), -ECONNRESET);
	}
}

/*
*******************************************************************************
*                     sw_udc_clean_dma_status
*
* Description:
*    清空ep关于DMA的所有状态, 一般在DMA异常的时间调用
*
* Parameters:
*    qh  :  input.
*
* Return value:
*    void
*
* note:
*    called with controller irqlocked
*
*******************************************************************************
*/
void sw_udc_clean_dma_status(struct sw_udc_ep *ep)
{
    u8 ep_index = 0;
	u8 old_ep_index = 0;
	struct sw_udc_request	*req = NULL;

	ep_index = ep->bEndpointAddress & 0x7F;

	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, ep_index);

	if((ep->bEndpointAddress) & USB_DIR_IN){  //dma_mode1
		/* clear ep dma status */
		USBC_Dev_ClearEpDma(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);

		/* select bus to pio */
		sw_udc_switch_bus_to_pio(ep, 1);
	}else{  //dma_mode0
		/* clear ep dma status */
		USBC_Dev_ClearEpDma(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);

		/* select bus to pio */
		sw_udc_switch_bus_to_pio(ep, 0);
	}

 	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	/* done req */
	while(likely (!list_empty(&ep->queue))){
		req = list_entry(ep->queue.next, struct sw_udc_request, queue);
		if(req){
			req->req.status = -ECONNRESET;
			req->req.actual = 0;
			sw_udc_done(ep, req, -ECONNRESET);
		}
	}

	return;
}

/*
*******************************************************************************
*                     sw_udc_stop_dma_work
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_stop_dma_work(struct sw_udc *dev, u32 unlock)
{
	__u32 i = 0;
	struct sw_udc_ep *ep = NULL;

	for(i = 0; i < SW_UDC_ENDPOINTS; i++){
		ep = &dev->ep[i];

		if(sw_udc_dma_is_busy(ep)){
			DMSG_PANIC("wrn: ep(%d) must stop working\n", i);

			if(unlock){
			    spin_unlock(&ep->dev->lock);
			}
			sw_udc_dma_stop(ep);
			if(unlock){
    			spin_lock(&ep->dev->lock);
			}

			sw_udc_clean_dma_status(ep);
		}
	}

	return;
}

/*
*******************************************************************************
*                     sw_udc_irq
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static irqreturn_t sw_udc_irq(int dummy, void *_dev)
{
	struct sw_udc *dev = _dev;
	int usb_irq     = 0;
	int tx_irq      = 0;
	int rx_irq      = 0;
	int i           = 0;
	u32 old_ep_idx  = 0;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->lock, flags);

	/* Driver connected ? */
	if (!dev->driver || !is_peripheral_active()) {
		DMSG_PANIC("ERR: functoin driver is not exist, or udc is not active.\n");

		/* Clear interrupts */
		clear_all_irq();

		spin_unlock_irqrestore(&dev->lock, flags);

		return IRQ_NONE;
	}

	/* Save index */
	old_ep_idx = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);

	/* Read status registers */
	usb_irq = USBC_INT_MiscPending(g_sw_udc_io.usb_bsp_hdle);
	tx_irq  = USBC_INT_EpPending(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);
	rx_irq  = USBC_INT_EpPending(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);

	usb_irq = filtrate_irq_misc(usb_irq);

	if(g_irq_debug){
		printk("\n\nirq: usb_irq=%02x, tx_irq=%02x, rx_irq=%02x\n", usb_irq, tx_irq, rx_irq);
	}

	/*
	 * Now, handle interrupts. There's two types :
	 * - Reset, Resume, Suspend coming -> usb_int_reg
	 * - EP -> ep_int_reg
	 */

	/* RESET */
	if (usb_irq & USBC_INTUSB_RESET) {
		DMSG_INFO_UDC("IRQ: reset\n");
		printk("(1:star,2:end): vfs_read:%d, vfs_write:%d,dma_working:%d,amount:%u,file_offset:%llu, vfs_time:(%llu, s:%llu, e:%llu), write_fifo: %d\n",
				atomic_read(&vfs_read_flag), atomic_read(&vfs_write_flag), dma_working, vfs_amount, (unsigned long long)vfs_file_offset,
				(sched_clock()- vfs_write_start) ,vfs_write_start, vfs_write_end, write_fifo_flag);

        USBC_INT_ClearMiscPending(g_sw_udc_io.usb_bsp_hdle, USBC_INTUSB_RESET);
        clear_all_irq();

		usb_connect = 1;

		USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, 0);
		USBC_Dev_SetAddress_default(g_sw_udc_io.usb_bsp_hdle);

		if(is_udc_support_dma()){
			sw_udc_stop_dma_work(dev, 1);
		}

        throw_away_all_urb(dev);

		dev->address = 0;
		dev->ep0state = EP0_IDLE;
//		dev->gadget.speed = USB_SPEED_FULL;

		spin_unlock_irqrestore(&dev->lock, flags);

		return IRQ_HANDLED;
	}

	/* RESUME */
	if (usb_irq & USBC_INTUSB_RESUME) {
		DMSG_INFO_UDC("IRQ: resume\n");

		/* clear interrupt */
		USBC_INT_ClearMiscPending(g_sw_udc_io.usb_bsp_hdle, USBC_INTUSB_RESUME);

		if(dev->gadget.speed != USB_SPEED_UNKNOWN
				&& dev->driver
				&& dev->driver->resume){
            spin_unlock(&dev->lock);
            dev->driver->resume(&dev->gadget);
            spin_lock(&dev->lock);
		}
	}

	/* SUSPEND */
	if (usb_irq & USBC_INTUSB_SUSPEND) {
		DMSG_INFO_UDC("IRQ: suspend\n");

		/* clear interrupt */
		USBC_INT_ClearMiscPending(g_sw_udc_io.usb_bsp_hdle, USBC_INTUSB_SUSPEND);

		if(dev->gadget.speed != USB_SPEED_UNKNOWN){
			/* disable usb controller */
			if (dev->driver && dev->driver->disconnect) {
				printk("disable usb controller\n");
                spin_unlock(&dev->lock);
				dev->driver->disconnect(&dev->gadget);
                spin_lock(&dev->lock);
			}
			usb_connect = 0;
		}else{
			DMSG_INFO_UDC("ERR: usb speed is unkown\n");
		}

		if (dev->gadget.speed != USB_SPEED_UNKNOWN
				&& dev->driver
				&& dev->driver->suspend) {
        		spin_unlock(&dev->lock);
			dev->driver->suspend(&dev->gadget);
			spin_lock(&dev->lock);
		}

		dev->ep0state = EP0_IDLE;

	}

    /* DISCONNECT */
    if(usb_irq & USBC_INTUSB_DISCONNECT){
        DMSG_INFO_UDC("IRQ: disconnect\n");
		USBC_INT_ClearMiscPending(g_sw_udc_io.usb_bsp_hdle, USBC_INTUSB_DISCONNECT);

		dev->ep0state = EP0_IDLE;

		usb_connect = 0;
	}

	/* EP */
	/* control traffic */
	/* check on ep0csr != 0 is not a good idea as clearing in_pkt_ready
	 * generate an interrupt
	 */
	if (tx_irq & USBC_INTTx_FLAG_EP0) {
		 DMSG_DBG_UDC("USB ep0 irq\n");

		/* Clear the interrupt bit by setting it to 1 */
		USBC_INT_ClearEpPending(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX, 0);

		if(dev->gadget.speed == USB_SPEED_UNKNOWN){
			if(USBC_Dev_QueryTransferMode(g_sw_udc_io.usb_bsp_hdle) == USBC_TS_MODE_HS){
				dev->gadget.speed = USB_SPEED_HIGH;

				DMSG_INFO_UDC("\n+++++++++++++++++++++++++++++++++++++\n");
			    DMSG_INFO_UDC(" usb enter high speed.\n");
			    DMSG_INFO_UDC("\n+++++++++++++++++++++++++++++++++++++\n");
			}else{
				dev->gadget.speed= USB_SPEED_FULL;

				DMSG_INFO_UDC("\n+++++++++++++++++++++++++++++++++++++\n");
			    DMSG_INFO_UDC(" usb enter full speed.\n");
			    DMSG_INFO_UDC("\n+++++++++++++++++++++++++++++++++++++\n");
			}
		}

		sw_udc_handle_ep0(dev);
	}

	/* 优先取数据 */

	/* rx endpoint data transfers */
	for (i = 1; i < SW_UDC_ENDPOINTS; i++) {
		u32 tmp = 1 << i;

		if (rx_irq & tmp) {
			DMSG_DBG_UDC("USB rx ep%d irq\n", i);

			/* Clear the interrupt bit by setting it to 1 */
			USBC_INT_ClearEpPending(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX, i);

			sw_udc_handle_ep(&dev->ep[ep_fifo_out[i]]);
		}
	}

	if(atomic_read(&ep_num_dma_rx)){

		u32 tmp = 1 << atomic_read(&ep_num_dma_rx);

		if(!(rx_irq & tmp)){
			sw_udc_handle_ep(&dev->ep[ep_fifo_out[atomic_read(&ep_num_dma_rx)]]);
		}
		atomic_set(&ep_num_dma_rx, 0);
	}

	/* tx endpoint data transfers */
	for (i = 1; i < SW_UDC_ENDPOINTS; i++) {
		u32 tmp = 1 << i;

		if (tx_irq & tmp) {
			DMSG_DBG_UDC("USB tx ep%d irq\n", i);

			/* Clear the interrupt bit by setting it to 1 */
			USBC_INT_ClearEpPending(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX, i);

			sw_udc_handle_ep(&dev->ep[ep_fifo_in[i]]);
		}
	}

	/* Restore old index */
	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_idx);

	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

/*
*******************************************************************************
*                     sw_udc_dma_completion
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void sw_udc_dma_completion(struct sw_udc *dev, struct sw_udc_ep *ep, struct sw_udc_request *req)
{
	unsigned long	flags 				= 0;
	__u8  			old_ep_index 		= 0;
	__u32 			dma_transmit_len 	= 0;
	int 			is_complete			= 0;
	struct sw_udc_request *req_next		= NULL;

	if(dev == NULL || ep == NULL || req == NULL){
		DMSG_PANIC("ERR: argment invaild. (0x%p, 0x%p, 0x%p)\n", dev, ep, req);
		return;
	}

	if(!ep->dma->dma_working){
		DMSG_PANIC("ERR: dma is not work, can not callback\n");
		return;
	}

    sw_udc_unmap_dma_buffer(req, dev, ep);

	spin_lock_irqsave(&dev->lock, flags);

	old_ep_index = USBC_GetActiveEp(dev->sw_udc_io->usb_bsp_hdle);
	USBC_SelectActiveEp(dev->sw_udc_io->usb_bsp_hdle, ep->num);

	if((ep->bEndpointAddress) & USB_DIR_IN){  //tx, dma_mode1
		USBC_Dev_ClearEpDma(dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_TX);
	}else{  //rx, dma_mode0
		USBC_Dev_ClearEpDma(dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_RX);
	}

	dma_transmit_len = sw_udc_dma_transmit_length(ep, ((ep->bEndpointAddress) & USB_DIR_IN), (__u32)req->req.buf);
	if(dma_transmit_len < req->req.length){
		if((ep->bEndpointAddress) & USB_DIR_IN){
			USBC_Dev_ClearEpDma(dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_TX);
		}else{
			USBC_Dev_ClearEpDma(dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_RX);
		}
	}

    if(g_dma_debug){
	    DMSG_INFO("di: (0x%p, %d, %d)\n", &(req->req), req->req.length, req->req.actual);
    }

	ep->dma->req = NULL;
	ep->dma->dma_working = 0;
	ep->dma->dma_transfer_len = 0;

	dma_working = 0;

    /* 如果本次传输有数据没有传输完毕，得接着传输 */
	req->req.actual += dma_transmit_len;
	if(req->req.length > req->req.actual){
		if(((ep->bEndpointAddress & USB_DIR_IN) != 0)
			&& !USBC_Dev_IsWriteDataReady(dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_TX)){
			if(pio_write_fifo(ep, req)){
				req = NULL;
				is_complete = 1;
			}
		}else if(((ep->bEndpointAddress & USB_DIR_IN) == 0)
			&& USBC_Dev_IsReadDataReady(dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_RX)){
			if(pio_read_fifo(ep, req)){
				req = NULL;
				is_complete = 1;
			}
		}
	}else{	/* 如果DMA完成的传输了数据，就done */
		sw_udc_done(ep, req, 0);
		is_complete = 1;
	}

	//-------------------------------------------------
	//发起下一次传输
	//-------------------------------------------------
	if(is_complete){
		if(likely (!list_empty(&ep->queue))){
			req_next = list_entry(ep->queue.next, struct sw_udc_request, queue);
		}else{
			req_next = NULL;
	    }

		if(req_next){
				if(((ep->bEndpointAddress & USB_DIR_IN) != 0)){
					write_fifo_flag = 1;
					while(USBC_Dev_IsWriteDataReady(dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_TX));
					sw_udc_write_fifo(ep, req_next);
					write_fifo_flag = 2;
				}else if(((ep->bEndpointAddress & USB_DIR_IN) == 0)){

					if(USBC_Dev_IsReadDataReady(dev->sw_udc_io->usb_bsp_hdle, USBC_EP_TYPE_RX)){
						sw_udc_read_fifo(ep, req_next);
					}else{
						if(g_dma_debug)
							printk("set_rx\n");
						atomic_set(&ep_num_dma_rx, ep->num);
					}

			}
		}
	}

	USBC_SelectActiveEp(dev->sw_udc_io->usb_bsp_hdle, old_ep_index);

	spin_unlock_irqrestore(&dev->lock, flags);

	return;
}

/*------------------------- sw_udc_ep_ops ----------------------------------*/

static inline struct sw_udc_ep *to_sw_udc_ep(struct usb_ep *ep)
{
	return container_of(ep, struct sw_udc_ep, ep);
}

static inline struct sw_udc *to_sw_udc(struct usb_gadget *gadget)
{
	return container_of(gadget, struct sw_udc, gadget);
}

static inline struct sw_udc_request *to_sw_udc_req(struct usb_request *req)
{
	return container_of(req, struct sw_udc_request, req);
}

/*
*******************************************************************************
*                     sw_udc_ep_enable
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_ep_enable(struct usb_ep *_ep,
				 const struct usb_endpoint_descriptor *desc)
{
	struct sw_udc	*dev			= NULL;
	struct sw_udc_ep	*ep				= NULL;
	u32			 	max     		= 0;
    u32     		old_ep_index	= 0;
	unsigned long flags = 0;

    u32 ep_type   = 0;
	u32 ts_type   = 0;
	u32 fifo_addr = 0;
	u32 fifo_size = 0;
	u8  double_fifo = 0;
	int i = 0;

	if(_ep == NULL || desc == NULL){
		DMSG_PANIC("ERR: invalid argment\n");
		return -EINVAL;
	}

	if (_ep->name == ep0name || desc->bDescriptorType != USB_DT_ENDPOINT){
		DMSG_PANIC("PANIC : _ep->name(%s) == ep0name || desc->bDescriptorType(%d) != USB_DT_ENDPOINT\n",
				   _ep->name , desc->bDescriptorType);
		return -EINVAL;
	}

	ep = to_sw_udc_ep(_ep);
    if(ep == NULL){
		DMSG_PANIC("ERR: usbd_ep_enable, ep = NULL\n");
		return -EINVAL;
	}

	if(ep->desc){
		DMSG_PANIC("ERR: usbd_ep_enable, ep->desc is not NULL, ep%d(%s)\n", ep->num, _ep->name);
		return -EINVAL;
	}

	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN){
		DMSG_PANIC("PANIC : dev->driver = 0x%p ?= NULL  dev->gadget->speed =%d ?= USB_SPEED_UNKNOWN\n",
				   dev->driver ,dev->gadget.speed);
		return -ESHUTDOWN;
    }

	spin_lock_irqsave(&ep->dev->lock, flags);

	max = le16_to_cpu(desc->wMaxPacketSize) & 0x1fff;
	_ep->maxpacket          = max & 0x7ff;
	ep->desc                = desc;
	ep->halted              = 0;
	ep->bEndpointAddress    = desc->bEndpointAddress;

    //ep_type
	if ((ep->bEndpointAddress) & USB_DIR_IN){ /* tx */
        ep_type = USBC_EP_TYPE_TX;
	}else{	 /* rx */
        ep_type = USBC_EP_TYPE_RX;
	}

    //ts_type
    switch(desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK){
        case USB_ENDPOINT_XFER_CONTROL:
            ts_type   = USBC_TS_TYPE_CTRL;
        break;

        case USB_ENDPOINT_XFER_BULK:
            ts_type   = USBC_TS_TYPE_BULK;
        break;

        case USB_ENDPOINT_XFER_ISOC:
            ts_type   = USBC_TS_TYPE_ISO;
       break;

        case USB_ENDPOINT_XFER_INT:
            ts_type = USBC_TS_TYPE_INT;
        break;

        default:
            DMSG_PANIC("err: usbd_ep_enable, unkown ep type(%d)\n", (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK));
            goto end;
    }

	//fifo_addr && fifo_size && double fifo
	for(i = 0; i < SW_UDC_ENDPOINTS; i++){
		if(!strcmp(_ep->name, ep_fifo[i].name)){
        	fifo_addr = ep_fifo[i].fifo_addr;
			fifo_size = ep_fifo[i].fifo_size;
			double_fifo = ep_fifo[i].double_fifo;
			break;
		}
	}

	DMSG_INFO_UDC("ep enable: ep%d(0x%p, %s, %d, %d), fifo(%d, %d, %d)\n",
		          ep->num, _ep, _ep->name, (desc->bEndpointAddress & USB_DIR_IN), _ep->maxpacket,
		          fifo_addr, fifo_size, double_fifo);

	if(i >= SW_UDC_ENDPOINTS){
		DMSG_PANIC("err: usbd_ep_enable, config fifo failed\n");
        goto end;
	}

    /* check fifo size */
	if((_ep->maxpacket & 0x7ff) > fifo_size){
		DMSG_PANIC("err: usbd_ep_enable, fifo size is too small\n");
        goto end;
	}

    /* check double fifo */
	if(double_fifo){
		if(((_ep->maxpacket & 0x7ff) * 2) > fifo_size){
			DMSG_PANIC("err: usbd_ep_enable, it is double fifo, but fifo size is too small\n");
	        goto end;
		}

		fifo_size = _ep->maxpacket & 0x7ff;
	}

	if(!is_peripheral_active()){
		DMSG_INFO("usbd_ep_enable, usb device is not active\n");
		goto end;
	}

    old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
    USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, ep->num);

	USBC_Dev_ConfigEp_Default(g_sw_udc_io.usb_bsp_hdle, ep_type);
	USBC_Dev_FlushFifo(g_sw_udc_io.usb_bsp_hdle, ep_type);

  	//set max packet ,type, direction, address; reset fifo counters, enable irq
    USBC_Dev_ConfigEp(g_sw_udc_io.usb_bsp_hdle, ts_type, ep_type, double_fifo, (_ep->maxpacket & 0x7ff));
	USBC_ConfigFifo(g_sw_udc_io.usb_bsp_hdle, ep_type, double_fifo, fifo_size, fifo_addr);
	if(ts_type == USBC_TS_TYPE_ISO){
	    USBC_Dev_IsoUpdateEnable(g_sw_udc_io.usb_bsp_hdle);
	}
    USBC_INT_EnableEp(g_sw_udc_io.usb_bsp_hdle, ep_type, ep->num);

    USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	/* dma channel */
	if(_ep->dma_flag && is_udc_support_dma()){
		sw_udc_dma_channel_init(dev, ep);
	}

end:
	spin_unlock_irqrestore(&ep->dev->lock, flags);

	sw_udc_set_halt(_ep, 0);

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_ep_disable
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_ep_disable(struct usb_ep *_ep)
{
	struct sw_udc_ep   	*ep     = NULL;
    u32 old_ep_index            = 0;
	unsigned long flags = 0;

	if (!_ep) {
		DMSG_PANIC("ERR: invalid argment\n");
		return -EINVAL;
	}

	ep = to_sw_udc_ep(_ep);
	if(ep == NULL){
		DMSG_PANIC("ERR: usbd_ep_disable: ep = NULL\n");
		return -EINVAL;
	}

	if (!ep->desc) {
		DMSG_PANIC("ERR: %s not enabled\n", _ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	DMSG_INFO_UDC("ep disable: ep%d(0x%p, %s, %d, %x)\n",
		          ep->num, _ep, _ep->name,
		          (ep->bEndpointAddress & USB_DIR_IN), _ep->maxpacket);

	spin_lock_irqsave(&ep->dev->lock, flags);

	DMSG_DBG_UDC("ep_disable: %s\n", _ep->name);

	ep->desc = NULL;
	ep->halted = 1;

	sw_udc_nuke (ep->dev, ep, -ESHUTDOWN);

	if(!is_peripheral_active()){
		DMSG_INFO("usb device is not active\n");
		goto end;
	}

	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
    USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, ep->num);

	if ((ep->bEndpointAddress) & USB_DIR_IN){ /* tx */
	    USBC_Dev_ConfigEp_Default(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);
		USBC_INT_DisableEp(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX, ep->num);
	}else{ /* rx */
	    USBC_Dev_ConfigEp_Default(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
		USBC_INT_DisableEp(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX, ep->num);
	}

	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	if(_ep->dma_flag && is_udc_support_dma()){
		sw_udc_dma_channel_exit(ep->dev, ep);
	}

end:
	spin_unlock_irqrestore(&ep->dev->lock, flags);

	DMSG_DBG_UDC("%s disabled\n", _ep->name);

	return 0;
}

/*
*******************************************************************************
*                     FunctionName
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static struct usb_request * sw_udc_alloc_request(struct usb_ep *_ep, gfp_t mem_flags)
{
	struct sw_udc_request *req = NULL;

	if(!_ep){
	    DMSG_PANIC("ERR: invalid argment\n");
		return NULL;
	}

	req = kzalloc (sizeof(struct sw_udc_request), mem_flags);
	if(!req){
	    DMSG_PANIC("ERR: kzalloc failed\n");
		return NULL;
	}


    memset(req, 0, sizeof(struct sw_udc_request));

	req->req.dma = DMA_ADDR_INVALID;

	INIT_LIST_HEAD (&req->queue);

	DMSG_INFO_UDC("alloc request: ep(0x%p, %s, %d), req(0x%p)\n",
		          _ep, _ep->name, _ep->maxpacket, req);

	return &req->req;
}

/*
*******************************************************************************
*                     sw_udc_free_request
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct sw_udc_request	*req = NULL;

	if(_ep == NULL || _req == NULL){
	    DMSG_PANIC("ERR: invalid argment\n");
		return;
	}

	req = to_sw_udc_req(_req);
	if(req == NULL){
	    DMSG_PANIC("ERR: invalid argment\n");
		return;
	}

	DMSG_INFO_UDC("free request: ep(0x%p, %s, %d), req(0x%p)\n",
		      _ep, _ep->name, _ep->maxpacket, req);

	kfree(req);

	return;
}

/*
*******************************************************************************
*                     FunctionName
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct sw_udc_request	*req    	= NULL;
	struct sw_udc_ep	    *ep     	= NULL;
	struct sw_udc	    	*dev    	= NULL;
	unsigned long flags = 0;
	u8 old_ep_index = 0;

	if(_ep == NULL || _req == NULL ){
        DMSG_PANIC("ERR: invalid argment\n");
		return -EINVAL;
	}

    ep = to_sw_udc_ep(_ep);
    if ((ep == NULL || (!ep->desc && _ep->name != ep0name))){
        DMSG_PANIC("ERR: sw_udc_queue: inval 2\n");
        return -EINVAL;
    }

	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN){
		DMSG_PANIC("ERR : dev->driver=0x%p, dev->gadget.speed=%x\n",
			       dev->driver, dev->gadget.speed);
		return -ESHUTDOWN;
	}

	if (!_req->complete || !_req->buf){
       	DMSG_PANIC("ERR: usbd_queue: _req is invalid\n");
        return -EINVAL;
	}

    req = to_sw_udc_req(_req);
	if (!req){
        DMSG_PANIC("ERR: req is NULL\n");
        return -EINVAL;
	}

	spin_lock_irqsave(&ep->dev->lock, flags);
	_req->status = -EINPROGRESS;
	_req->actual = 0;
	spin_unlock_irqrestore(&ep->dev->lock, flags);

    if(is_sw_udc_dma_capable(req, ep)){
        sw_udc_map_dma_buffer(req, dev, ep);
    }

	spin_lock_irqsave(&ep->dev->lock, flags);

	list_add_tail(&req->queue, &ep->queue);

	if(!is_peripheral_active()){
		DMSG_PANIC("warn: peripheral is active\n");
		goto end;
	}

    if(g_queue_debug){
	    DMSG_INFO("q: (%s, %p, %d, %d)\n", _ep->name, _req, _req->length, _req->actual);
	}

	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
	if (ep->bEndpointAddress) {
		USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, ep->bEndpointAddress & 0x7F);
	} else {
		USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, 0);
	}

	/* 如果队列中只有一个,那么就被执行 */
	if (!ep->halted && (&req->queue == ep->queue.next)) {
		if (ep->bEndpointAddress == 0 /* ep0 */) {
			switch (dev->ep0state) {
    			case EP0_IN_DATA_PHASE:
    				if (!USBC_Dev_IsWriteDataReady(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX)
    				    && sw_udc_write_fifo(ep, req)) {
    					dev->ep0state = EP0_IDLE;
    					req = NULL;
    				}
				break;

    			case EP0_OUT_DATA_PHASE:
    				if ((!_req->length)
    					|| (USBC_Dev_IsReadDataReady(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX)
    						&& sw_udc_read_fifo(ep, req))) {
    					dev->ep0state = EP0_IDLE;
    					req = NULL;
    				}
				break;

    			default:
    				spin_unlock_irqrestore(&ep->dev->lock, flags);
				return -EL2HLT;
			}
		} else if ((ep->bEndpointAddress & USB_DIR_IN) != 0
				&& !USBC_Dev_IsWriteDataReady(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX)) {
			if(sw_udc_write_fifo(ep, req)){
				req = NULL;
			}
		} else if ((ep->bEndpointAddress & USB_DIR_IN) == 0
				&& USBC_Dev_IsReadDataReady(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX)) {
			if(sw_udc_read_fifo(ep, req)){
				req = NULL;
			}
		}
	}

	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

end:
	spin_unlock_irqrestore(&ep->dev->lock, flags);

    return 0;
}

/*
*******************************************************************************
*                     sw_udc_dequeue
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct sw_udc_ep	*ep  	= NULL;
	struct sw_udc		*udc  	= NULL;
	int			        retval  = -EINVAL;
	struct sw_udc_request *req 	= NULL;
	unsigned long flags = 0;

	DMSG_DBG_UDC("(%p,%p)\n", _ep, _req);

	if(!the_controller->driver){
	    DMSG_PANIC("ERR: sw_udc_dequeue: driver is null\n");
		return -ESHUTDOWN;
    }

	if(!_ep || !_req){
	    DMSG_PANIC("ERR: sw_udc_dequeue: invalid argment\n");
		return retval;
    }

	ep = to_sw_udc_ep(_ep);
	if(ep == NULL){
		DMSG_PANIC("ERR: ep == NULL\n");
		return -EINVAL;
	}

	udc = to_sw_udc(ep->gadget);
	if(udc == NULL){
		DMSG_PANIC("ERR: ep == NULL\n");
		return -EINVAL;
	}

	DMSG_INFO_UDC("dequeue: ep(0x%p, %d), _req(0x%p, %d, %d)\n",
		      ep, ep->num,
		      _req, _req->length, _req->actual);

	spin_lock_irqsave(&ep->dev->lock, flags);

	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init (&req->queue);
			_req->status = -ECONNRESET;
			retval = 0;
			break;
		}
	}

	if (retval == 0) {
		DMSG_DBG_UDC("dequeued req %p from %s, len %d buf %p\n",
			        req, _ep->name, _req->length, _req->buf);

		sw_udc_done(ep, req, -ECONNRESET);
	}

	spin_unlock_irqrestore(&ep->dev->lock, flags);

	return retval;
}

/*
*******************************************************************************
*                     sw_udc_set_halt_ex
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_set_halt_ex(struct usb_ep *_ep, int value)
{
	struct sw_udc_ep		*ep     = NULL;
	u32			        idx     = 0;
	__u8    old_ep_index        = 0;

	if(_ep == NULL){
		DMSG_PANIC("ERR: invalid argment\n");
		return -EINVAL;
	}

	ep = to_sw_udc_ep(_ep);
	if(ep == NULL){
		DMSG_PANIC("ERR: invalid argment\n");
		return -EINVAL;
	}

	if(!ep->desc && ep->ep.name != ep0name){
		DMSG_PANIC("ERR: !ep->desc && ep->ep.name != ep0name\n");
		return -EINVAL;
	}

	if(!is_peripheral_active()){
		DMSG_INFO("ERR: usb device is not active\n");
		return 0;
	}

	idx = ep->bEndpointAddress & 0x7F;

	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
    USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, idx);

	if (idx == 0) {
		USBC_Dev_EpClearStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);
	} else {
		if ((ep->bEndpointAddress & USB_DIR_IN) != 0) {
			if(value){
				USBC_Dev_EpSendStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);
			}else{
				USBC_Dev_EpClearStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);
			}
		} else {
			if(value){
				USBC_Dev_EpSendStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
			}else{
				USBC_Dev_EpClearStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
			}
		}
	}

	ep->halted = value ? 1 : 0;

	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_set_halt
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_set_halt(struct usb_ep *_ep, int value)
{
	struct sw_udc_ep		*ep     = NULL;
	unsigned long flags = 0;
	u32			        idx     = 0;
	__u8    old_ep_index        = 0;

	if(_ep == NULL){
		DMSG_PANIC("ERR: invalid argment\n");
		return -EINVAL;
	}

	ep = to_sw_udc_ep(_ep);
	if(ep == NULL){
		DMSG_PANIC("ERR: invalid argment\n");
		return -EINVAL;
	}

	if(!ep->desc && ep->ep.name != ep0name){
		DMSG_PANIC("ERR: !ep->desc && ep->ep.name != ep0name\n");
		return -EINVAL;
	}

	if(!is_peripheral_active()){
		DMSG_INFO("ERR: usb device is not active\n");
		return 0;
	}

	spin_lock_irqsave(&ep->dev->lock, flags);

	idx = ep->bEndpointAddress & 0x7F;

	old_ep_index = USBC_GetActiveEp(g_sw_udc_io.usb_bsp_hdle);
    USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, idx);

	if (idx == 0) {
		USBC_Dev_EpClearStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_EP0);
	} else {
		if ((ep->bEndpointAddress & USB_DIR_IN) != 0) {
			if(value){
				USBC_Dev_EpSendStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);
			}else{
				USBC_Dev_EpClearStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);
			}
		} else {
			if(value){
				USBC_Dev_EpSendStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
			}else{
				USBC_Dev_EpClearStall(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
			}
		}
	}

	ep->halted = value ? 1 : 0;

	USBC_SelectActiveEp(g_sw_udc_io.usb_bsp_hdle, old_ep_index);

	spin_unlock_irqrestore(&ep->dev->lock, flags);

	return 0;
}

static const struct usb_ep_ops sw_udc_ep_ops = {
	.enable			= sw_udc_ep_enable,
	.disable		= sw_udc_ep_disable,

	.alloc_request	= sw_udc_alloc_request,
	.free_request	= sw_udc_free_request,

	.queue			= sw_udc_queue,
	.dequeue		= sw_udc_dequeue,

	.set_halt		= sw_udc_set_halt,
};


/*
*******************************************************************************
*                     sw_udc_get_frame
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_get_frame(struct usb_gadget *_gadget)
{
	if(!is_peripheral_active()){
		DMSG_INFO("ERR: usb device is not active\n");
		return 0;
	}

	return (int)USBC_REG_FRNUM(g_sw_udc_io.usb_vbase);
}

/*
*******************************************************************************
*                     sw_udc_wakeup
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_wakeup(struct usb_gadget *_gadget)
{
	if(!is_peripheral_active()){
		DMSG_INFO("ERR: usb device is not active\n");
		return 0;
	}

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_set_selfpowered
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_set_selfpowered(struct usb_gadget *gadget, int value)
{
	if(!is_peripheral_active()){
		DMSG_INFO("ERR: usb device is not active\n");
		return 0;
	}

	return 0;
}

static void sw_udc_disable(struct sw_udc *dev);
static void sw_udc_enable(struct sw_udc *dev);

/*
*******************************************************************************
*                     FunctionName
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_set_pullup(struct sw_udc *udc, int is_on)
{
	DMSG_DBG_UDC("sw_udc_set_pullup\n");

    is_udc_enable = is_on;

	if(!is_peripheral_active()){
		DMSG_INFO("usb device is not active\n");
		return 0;
	}

	if(is_on){
		sw_udc_enable(udc);
	}else{
		if (udc->gadget.speed != USB_SPEED_UNKNOWN) {
			if (udc->driver && udc->driver->disconnect)
				udc->driver->disconnect(&udc->gadget);
		}

		sw_udc_disable(udc);
	}

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_vbus_session
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct sw_udc *udc = to_sw_udc(gadget);

	DMSG_DBG_UDC("sw_udc_vbus_session\n");

	if(!is_peripheral_active()){
		DMSG_INFO("usb device is not active\n");
		return 0;
	}

	udc->vbus = (is_active != 0);
	sw_udc_set_pullup(udc, is_active);

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_pullup
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct sw_udc *udc = to_sw_udc(gadget);

	DMSG_INFO_UDC("sw_udc_pullup, is_on = %d\n", is_on);

	sw_udc_set_pullup(udc, is_on);

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_vbus_draw
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_vbus_draw(struct usb_gadget *_gadget, unsigned ma)
{
	if(!is_peripheral_active()){
		DMSG_INFO("ERR: usb device is not active\n");
		return 0;
	}

	DMSG_DBG_UDC("sw_udc_vbus_draw\n");

	cfg_vbus_draw(ma);

	return 0;
}

//---------------------------------------------------------------
//   gadget driver handling
//---------------------------------------------------------------

/*
*******************************************************************************
*                     sw_udc_reinit
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_reinit(struct sw_udc *dev)
{
	u32 i = 0;

	/* device/ep0 records init */
	INIT_LIST_HEAD (&dev->gadget.ep_list);
	INIT_LIST_HEAD (&dev->gadget.ep0->ep_list);
	dev->ep0state = EP0_IDLE;

	for (i = 0; i < SW_UDC_ENDPOINTS; i++) {
		struct sw_udc_ep *ep = &dev->ep[i];

		if (i != 0) {
			list_add_tail (&ep->ep.ep_list, &dev->gadget.ep_list);
        }

		ep->dev     = dev;
		ep->desc    = NULL;
		ep->halted  = 0;
		INIT_LIST_HEAD (&ep->queue);
	}

	return;
}

/*
*******************************************************************************
*                     FunctionName
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_enable(struct sw_udc *dev)
{
	DMSG_DBG_UDC("sw_udc_enable called\n");

	/* dev->gadget.speed = USB_SPEED_UNKNOWN; */
	dev->gadget.speed = USB_SPEED_UNKNOWN;

#ifdef	CONFIG_USB_GADGET_DUALSPEED
	DMSG_INFO_UDC("CONFIG_USB_GADGET_DUALSPEED: USBC_TS_MODE_HS\n");

	USBC_Dev_ConfigTransferMode(g_sw_udc_io.usb_bsp_hdle, USBC_TS_TYPE_BULK, USBC_TS_MODE_HS);
#else
	DMSG_INFO_UDC("CONFIG_USB_GADGET_DUALSPEED: USBC_TS_MODE_FS\n");

	USBC_Dev_ConfigTransferMode(g_sw_udc_io.usb_bsp_hdle, USBC_TS_TYPE_BULK, USBC_TS_MODE_FS);
#endif

	/* Enable reset and suspend interrupt interrupts */
	USBC_INT_EnableUsbMiscUint(g_sw_udc_io.usb_bsp_hdle, USBC_BP_INTUSB_SUSPEND);
	USBC_INT_EnableUsbMiscUint(g_sw_udc_io.usb_bsp_hdle, USBC_BP_INTUSB_RESUME);
	USBC_INT_EnableUsbMiscUint(g_sw_udc_io.usb_bsp_hdle, USBC_BP_INTUSB_RESET);
	USBC_INT_EnableUsbMiscUint(g_sw_udc_io.usb_bsp_hdle, USBC_BP_INTUSB_DISCONNECT);

	/* Enable ep0 interrupt */
	USBC_INT_EnableEp(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX, 0);

	cfg_udc_command(SW_UDC_P_ENABLE);

	return ;
}

/*
*******************************************************************************
*                     sw_udc_disable
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_disable(struct sw_udc *dev)
{
	DMSG_DBG_UDC("sw_udc_disable\n");

	/* Disable all interrupts */
    USBC_INT_DisableUsbMiscAll(g_sw_udc_io.usb_bsp_hdle);
    USBC_INT_DisableEpAll(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_RX);
    USBC_INT_DisableEpAll(g_sw_udc_io.usb_bsp_hdle, USBC_EP_TYPE_TX);

	/* Clear the interrupt registers */
	clear_all_irq();
	cfg_udc_command(SW_UDC_P_DISABLE);

	/* Set speed to unknown */
	dev->gadget.speed = USB_SPEED_UNKNOWN;

	return;
}

s32  usbd_start_work(void)
{
	DMSG_INFO_UDC("usbd_start_work\n");

	if(!is_peripheral_active()){
		DMSG_INFO("usb device is not active\n");
		return 0;
	}

	enable_irq_udc(the_controller);
	USBC_Dev_ConectSwitch(g_sw_udc_io.usb_bsp_hdle, USBC_DEVICE_SWITCH_ON);

	return 0;
}

s32  usbd_stop_work(void)
{
	DMSG_INFO_UDC("usbd_stop_work\n");

	if(!is_peripheral_active()){
		DMSG_INFO("ERR: usb device is not active\n");
		return 0;
	}

	disable_irq_udc(the_controller);
    USBC_Dev_ConectSwitch(g_sw_udc_io.usb_bsp_hdle, USBC_DEVICE_SWITCH_OFF);	//默认为pulldown

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_start
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
int sw_udc_start(struct usb_gadget_driver *driver,
		                    int (*bind)(struct usb_gadget *))
{
	struct sw_udc *udc = the_controller;
	int retval = 0;

	/* Sanity checks */
	if(!udc){
	    DMSG_PANIC("ERR: udc is null\n");
		return -ENODEV;
    }

	if (udc->driver){
	    DMSG_PANIC("ERR: udc->driver is not null\n");
		return -EBUSY;
    }

	/*linux 3.0和3.3的数据结构usb_gadget_driver有小改动speed->max_spee */
	if (!bind || !driver->setup || driver->max_speed < USB_SPEED_FULL) {
		DMSG_PANIC("ERR: Invalid driver: bind %p setup %p speed %d\n",
			       bind, driver->setup, driver->max_speed);
		return -EINVAL;
	}

#if defined(MODULE)
    if (!driver->unbind) {
        DMSG_PANIC("Invalid driver: no unbind method\n");
        return -EINVAL;
    }
#endif

	/* Hook the driver */
	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;

	/* Bind the driver */
	if ((retval = device_add(&udc->gadget.dev)) != 0) {
		DMSG_PANIC("ERR: Error in device_add() : %d\n",retval);
		goto register_error;
	}

	DMSG_INFO_UDC("[%s]: binding gadget driver '%s'\n", gadget_name, driver->driver.name);

	if ((retval = bind (&udc->gadget)) != 0) {
	    DMSG_PANIC("ERR: Error in bind() : %d\n",retval);
		device_del(&udc->gadget.dev);
		goto register_error;
	}

	return 0;

register_error:
	udc->driver = NULL;
	udc->gadget.dev.driver = NULL;

	return retval;
}

/*
*******************************************************************************
*                     sw_udc_stop
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
int sw_udc_stop(struct usb_gadget_driver *driver)
{
	struct sw_udc *udc = the_controller;

	if(!udc){
	    DMSG_PANIC("ERR: udc is null\n");
		return -ENODEV;
    }

	if(!driver || driver != udc->driver || !driver->unbind){
	    DMSG_PANIC("ERR: driver is null\n");
		return -EINVAL;
    }

	DMSG_INFO_UDC("[%s]: usb_gadget_unregister_driver() '%s'\n", gadget_name, driver->driver.name);

	if(driver->disconnect){
		driver->disconnect(&udc->gadget);
    }

    /* unbind gadget driver */
	driver->unbind(&udc->gadget);
	udc->gadget.dev.driver = NULL;
	device_del(&udc->gadget.dev);
	udc->driver = NULL;

	/* Disable udc */
	sw_udc_disable(udc);

	return 0;
}

static const struct usb_gadget_ops sw_udc_ops = {
	.get_frame		    = sw_udc_get_frame,
	.wakeup			    = sw_udc_wakeup,
	.set_selfpowered	= sw_udc_set_selfpowered,
	.pullup			    = sw_udc_pullup,
	.vbus_session		= sw_udc_vbus_session,
	.vbus_draw		    = sw_udc_vbus_draw,
	.start				= sw_udc_start,
	.stop				= sw_udc_stop,
};

#if 0
static struct sw_udc sw_udc = {
	.gadget = {
		.ops		= &sw_udc_ops,
		.ep0		= &sw_udc.ep[0].ep,
		.name		= gadget_name,
		.dev = {
			.init_name	= "gadget",
		},
	},

	/* control endpoint */
	.ep[0] = {
		.num			= 0,
		.ep = {
			.name		= ep0name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= EP0_FIFO_SIZE,
		},
		.dev			= &sw_udc,
	},

	/* first group of endpoints */
	.ep[1] = {
		.num			= 1,
		.ep = {
			.name		= "ep1-bulk",
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		.fifo_size	        = (SW_UDC_EP_FIFO_SIZE * (SW_UDC_FIFO_NUM + 1)),
		.bEndpointAddress   = 1,
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},

	.ep[2] = {
		.num			= 2,
		.ep = {
			.name		= "ep2-bulk",
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		.fifo_size	        = (SW_UDC_EP_FIFO_SIZE * (SW_UDC_FIFO_NUM + 1)),
		.bEndpointAddress   = 2,
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},

	.ep[3] = {
		.num			= 3,
		.ep = {
			.name		= "ep3-bulk",
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		.fifo_size	        = (SW_UDC_EP_FIFO_SIZE * (SW_UDC_FIFO_NUM + 1)),
		.bEndpointAddress   = 3,
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},

	.ep[4] = {
		.num			= 4,
		.ep = {
			.name		= "ep4-bulk",
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		.fifo_size	        = (SW_UDC_EP_FIFO_SIZE * (SW_UDC_FIFO_NUM + 1)),
		.bEndpointAddress   = 4,
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},

	.ep[5] = {
		.num			= 5,
		.ep = {
			.name		= "ep5-int",
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		.fifo_size	        = (SW_UDC_EP_FIFO_SIZE * (SW_UDC_FIFO_NUM + 1)),
		.bEndpointAddress   = 5,
		.bmAttributes	    = USB_ENDPOINT_XFER_INT,
	},
};
#else
static struct sw_udc sw_udc = {
	.gadget = {
		.ops		= &sw_udc_ops,
		.ep0		= &sw_udc.ep[0].ep,
		.name		= gadget_name,
		.dev = {
			.init_name	= "gadget",
		},
	},

	.ep[0] = {
		.num			= 0,
		.ep = {
			.name		= ep0name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= EP0_FIFO_SIZE,
		},
		.dev			= &sw_udc,
	},

	.ep[1] = {
		.num			= 1,
		.ep = {
			.name		= ep1in_bulk_name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		//.fifo_size	        = SW_UDC_EP_FIFO_SIZE,
		.bEndpointAddress   = (USB_DIR_IN | 1),
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},

	.ep[2] = {
		.num			= 1,
		.ep = {
			.name		= ep1out_bulk_name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		//.fifo_size	        = SW_UDC_EP_FIFO_SIZE,
		.bEndpointAddress   = (USB_DIR_OUT | 1),
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},

	.ep[3] = {
		.num			= 2,
		.ep = {
			.name		= ep2in_bulk_name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		//.fifo_size	        = SW_UDC_EP_FIFO_SIZE,
		.bEndpointAddress   = (USB_DIR_IN | 2),
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},

	.ep[4] = {
		.num			= 2,
		.ep = {
			.name		= ep2out_bulk_name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		//.fifo_size	        = SW_UDC_EP_FIFO_SIZE,
		.bEndpointAddress   = (USB_DIR_OUT | 2),
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},

	.ep[5] = {
		.num			= 3,
		.ep = {
			.name		= ep3_iso_name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		//.fifo_size	        = SW_UDC_EP_FIFO_SIZE,
		.bEndpointAddress   = 3,
		.bmAttributes	    = USB_ENDPOINT_XFER_ISOC,
	},

	.ep[6] = {
		.num			= 4,
		.ep = {
			.name		= ep4_int_name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		//.fifo_size	        = SW_UDC_EP_FIFO_SIZE,
		.bEndpointAddress   = 4,
		.bmAttributes	    = USB_ENDPOINT_XFER_INT,
	},

	.ep[7] = {
		.num			= 5,
		.ep = {
			.name		= ep5in_bulk_name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		//.fifo_size	        = SW_UDC_EP_FIFO_SIZE,
		.bEndpointAddress   = (USB_DIR_IN | 5),
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},

	.ep[8] = {
		.num			= 5,
		.ep = {
			.name		= ep5out_bulk_name,
			.ops		= &sw_udc_ep_ops,
			.maxpacket	= SW_UDC_EP_FIFO_SIZE,
		},
		.dev		        = &sw_udc,
		//.fifo_size	        = SW_UDC_EP_FIFO_SIZE,
		.bEndpointAddress   = (USB_DIR_OUT | 5),
		.bmAttributes	    = USB_ENDPOINT_XFER_BULK,
	},
};
#endif

int sw_usb_device_enable(void)
{
	struct platform_device *pdev = g_udc_pdev;
	struct sw_udc  	*udc    = &sw_udc;
	int           	retval  = 0;
	int            	irq     = AW_IRQ_USB_OTG;

	DMSG_INFO_UDC("sw_usb_device_enable start\n");

	if(pdev == NULL){
		DMSG_PANIC("pdev is null\n");
		return -1;
	}

	usbd_port_no   	= 0;
	usb_connect 	= 0;
	crq_bRequest 	= 0;
	is_controller_alive = 1;

    memset(&g_sw_udc_io, 0, sizeof(sw_udc_io_t));

    retval = sw_udc_io_init(usbd_port_no, pdev, &g_sw_udc_io);
    if(retval != 0){
        DMSG_PANIC("ERR: sw_udc_io_init failed\n");
        return -1;
    }

	sw_udc_disable(udc);

	udc->sw_udc_io = &g_sw_udc_io;
	udc->usbc_no = usbd_port_no;
	strcpy((char *)udc->driver_name, gadget_name);
	udc->irq_no	 = irq;
	udc->pdev    = pdev;
	udc->controller = &(pdev->dev);

	if(is_udc_support_dma()){
		retval = sw_udc_dma_probe(udc);
		if(retval != 0){
			DMSG_PANIC("ERR: sw_udc_dma_probe failef\n");
			retval = -EBUSY;
			goto err;
		}
	}

	retval = request_irq(irq, sw_udc_irq,
			     IRQF_DISABLED, gadget_name, udc);
	if (retval != 0) {
		DMSG_PANIC("ERR: cannot get irq %i, err %d\n", irq, retval);
		retval = -EBUSY;
		goto err;
	}

	if(udc->driver && is_udc_enable){
		sw_udc_enable(udc);
		cfg_udc_command(SW_UDC_P_ENABLE);
	}

	DMSG_INFO_UDC("sw_usb_device_enable end\n");

    return 0;

err:
	if(is_udc_support_dma()){
		sw_udc_dma_remove(udc);
	}

    sw_udc_io_exit(usbd_port_no, pdev, &g_sw_udc_io);

    return retval;
}
EXPORT_SYMBOL(sw_usb_device_enable);

int sw_usb_device_disable(void)
__releases(sw_udc.lock)
__acquires(sw_udc.lock)
{
	struct platform_device *pdev = g_udc_pdev;
	struct sw_udc *udc 	= NULL;
	unsigned long	flags = 0;

	DMSG_INFO_UDC("sw_usb_device_disable start\n");

	if(pdev == NULL){
		DMSG_PANIC("pdev is null\n");
		return -1;
	}

	udc = platform_get_drvdata(pdev);
	if(udc == NULL){
		DMSG_PANIC("udc is null\n");
		return -1;
	}

    /* disable usb controller */
	if (udc->driver && udc->driver->disconnect) {
		udc->driver->disconnect(&udc->gadget);
	}

	if(is_udc_support_dma()){
		spin_lock_irqsave(&udc->lock, flags);
	    sw_udc_stop_dma_work(udc, 0);
		spin_unlock_irqrestore(&udc->lock, flags);
		sw_udc_dma_remove(udc);
	}

	free_irq(udc->irq_no, udc);

	usbd_port_no   = 0;
	usb_connect    = 0;
	crq_bRequest   = 0;
	is_controller_alive = 0;

	sw_udc_io_exit(usbd_port_no, pdev, &g_sw_udc_io);

    spin_lock_irqsave(&udc->lock, flags);

    memset(&g_sw_udc_io, 0, sizeof(sw_udc_io_t));

    spin_unlock_irqrestore(&udc->lock, flags);

	DMSG_INFO_UDC("sw_usb_device_disable end\n");

	return 0;
}
EXPORT_SYMBOL(sw_usb_device_disable);

/*
*******************************************************************************
*                     sw_udc_probe_otg
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_probe_otg(struct platform_device *pdev)
{
	struct sw_udc  	*udc = &sw_udc;
	int retval = 0;
	g_udc_pdev = pdev;

	spin_lock_init (&udc->lock);

	device_initialize(&udc->gadget.dev);
	udc->gadget.dev.parent = &pdev->dev;
	udc->gadget.dev.dma_mask = pdev->dev.dma_mask;

	sw_udc_reinit(udc);

	the_controller = udc;
	platform_set_drvdata(pdev, udc);

	retval = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if(retval){
		return retval;
	}

    device_create_file(&pdev->dev, &dev_attr_otg_ed_test);
    device_create_file(&pdev->dev, &dev_attr_queue_debug);
    device_create_file(&pdev->dev, &dev_attr_dma_debug);
    device_create_file(&pdev->dev, &dev_attr_write_debug);
    device_create_file(&pdev->dev, &dev_attr_read_debug);
    device_create_file(&pdev->dev, &dev_attr_irq_debug);
    device_create_file(&pdev->dev, &dev_attr_msc_read_debug);
    device_create_file(&pdev->dev, &dev_attr_msc_write_debug);

    return 0;
}

/*
*******************************************************************************
*                     sw_udc_remove_otg
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_remove_otg(struct platform_device *pdev)
{
	struct sw_udc *udc 	= NULL;

	g_udc_pdev = NULL;

	udc = platform_get_drvdata(pdev);
	if (udc->driver){
	    DMSG_PANIC("ERR: invalid argment, udc->driver(0x%p)\n", udc->driver);
		return -EBUSY;
    }

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_probe_device_only
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_probe_device_only(struct platform_device *pdev)
{
	struct sw_udc  	*udc    = &sw_udc;
//	struct device       *dev    = &pdev->dev;
	int                 retval  = 0;
	int                 irq     = AW_IRQ_USB_OTG;

    memset(&g_sw_udc_io, 0, sizeof(sw_udc_io_t));

    retval = sw_udc_io_init(usbd_port_no, pdev, &g_sw_udc_io);
    if(retval != 0){
        DMSG_PANIC("ERR: sw_udc_io_init failed\n");
        return -1;
    }

	spin_lock_init (&udc->lock);

	device_initialize(&udc->gadget.dev);
	udc->gadget.dev.parent = &pdev->dev;
	udc->gadget.dev.dma_mask = pdev->dev.dma_mask;

	is_controller_alive = 1;
	the_controller = udc;
	platform_set_drvdata(pdev, udc);

	sw_udc_disable(udc);
	sw_udc_reinit(udc);

	udc->sw_udc_io = &g_sw_udc_io;
	udc->usbc_no = usbd_port_no;
	strcpy((char *)udc->driver_name, gadget_name);
	udc->irq_no	 = irq;
	udc->pdev    = pdev;

	if(is_udc_support_dma()){
		retval = sw_udc_dma_probe(udc);
		if(retval != 0){
			DMSG_PANIC("ERR: sw_udc_dma_probe failef\n");
			retval = -EBUSY;
			goto err;
		}
	}

	retval = request_irq(irq, sw_udc_irq,
			     IRQF_DISABLED, gadget_name, udc);
	if (retval != 0) {
		DMSG_PANIC("ERR: cannot get irq %i, err %d\n", irq, retval);
		retval = -EBUSY;
		goto err;
	}

	retval = usb_add_gadget_udc(&pdev->dev, &udc->gadget);


	if (retval)
		goto err;

    return 0;

err:
	if(is_udc_support_dma()){
		sw_udc_dma_remove(udc);
	}

    sw_udc_io_exit(usbd_port_no, pdev, &g_sw_udc_io);

    return retval;
}

/*
*******************************************************************************
*                     sw_udc_remove_device_only
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_remove_device_only(struct platform_device *pdev)
{
	struct sw_udc *udc 	= platform_get_drvdata(pdev);

	if (udc->driver){
	    DMSG_PANIC("ERR: invalid argment\n");
		return -EBUSY;
    }

	if(is_udc_support_dma()){
	    sw_udc_stop_dma_work(udc, 0);
		sw_udc_dma_remove(udc);
	}

	free_irq(udc->irq_no, udc);

	sw_udc_io_exit(usbd_port_no, pdev, &g_sw_udc_io);

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_probe
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int __init sw_udc_probe(struct platform_device *pdev)
{
#ifdef  CONFIG_USB_SW_SUN6I_USB0_OTG
	struct sw_udc_mach_info *udc_cfg = pdev->dev.platform_data;

    switch(udc_cfg->port_info->port_type){
        case USB_PORT_TYPE_DEVICE:
            return sw_udc_probe_device_only(pdev);
        //break;

        case USB_PORT_TYPE_OTG:
            return sw_udc_probe_otg(pdev);
        //break;

        default:
            DMSG_PANIC("ERR: unkown port_type(%d)\n", udc_cfg->port_info->port_type);
    }

    return 0;
#else
    return sw_udc_probe_device_only(pdev);
#endif
}

static int __devexit sw_udc_remove(struct platform_device *pdev)
{
#ifdef  CONFIG_USB_SW_SUN6I_USB0_OTG
	struct sw_udc_mach_info *udc_cfg = pdev->dev.platform_data;

    switch(udc_cfg->port_info->port_type){
        case USB_PORT_TYPE_DEVICE:
            return sw_udc_remove_device_only(pdev);
        //break;

        case USB_PORT_TYPE_OTG:
            return sw_udc_remove_otg(pdev);
        //break;

        default:
            DMSG_PANIC("ERR: unkown port_type(%d)\n", udc_cfg->port_info->port_type);
    }

    return 0;
#else
    return sw_udc_remove_device_only(pdev);
#endif
}

/*
*******************************************************************************
*                     sw_udc_suspend
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_suspend(struct platform_device *pdev, pm_message_t message)
{
	struct sw_udc *udc = platform_get_drvdata(pdev);

    DMSG_INFO_UDC("sw_udc_suspend start\n");
	device_insmod_delay = 0;

	atomic_set(&thread_suspend_flag, 1);

	if(!is_peripheral_active()){
		DMSG_INFO_UDC("udc is disable, need not enter to suspend\n");
		return 0;
	}

    /* 如果 USB 没有接 PC, 就可以进入 suspend。
     * 如果 USB 接了 PC, 就不进入 suspend
     */
    //if(usb_connect){
    //    DMSG_PANIC("ERR: usb is connect to PC, can not suspend\n");
    //    return -EBUSY;
    // }

    /* soft disconnect */
	cfg_udc_command(SW_UDC_P_DISABLE);

    /* disable usb controller */
	if (udc->driver && udc->driver->disconnect) {
		udc->driver->disconnect(&udc->gadget);
	}

	sw_udc_disable(udc);

	/* close USB clock */
	close_usb_clock(&g_sw_udc_io);

    DMSG_INFO_UDC("sw_udc_suspend end\n");

	return 0;
}

/*
*******************************************************************************
*                     sw_udc_resume
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_udc_resume(struct platform_device *pdev)
{
	struct sw_udc *udc = platform_get_drvdata(pdev);

    DMSG_INFO_UDC("sw_udc_resume start\n");
	device_insmod_delay = 0;

	atomic_set(&thread_suspend_flag, 0);

	if(!is_peripheral_active()){
		DMSG_INFO_UDC("udc is disable, need not enter to resume\n");
		return 0;
	}

    /* open USB clock */
	open_usb_clock(&g_sw_udc_io);

	sw_udc_bsp_init(0, &g_sw_udc_io);

	if(is_udc_enable){
	    /* enable usb controller */
        sw_udc_enable(udc);

        /* soft connect */
        cfg_udc_command(SW_UDC_P_ENABLE);
    }

    DMSG_INFO_UDC("sw_udc_resume end\n");

	return 0;
}

static struct platform_driver sw_udc_driver = {
	.driver		= {
		.name	= (char *)gadget_name,
		.bus	= &platform_bus_type,
		.owner	= THIS_MODULE,
	},

	.probe		= sw_udc_probe,
	.remove		= __devexit_p(sw_udc_remove),
	.suspend	= sw_udc_suspend,
	.resume		= sw_udc_resume,
};

static void cfg_udc_command(enum sw_udc_cmd_e cmd)
{
	struct sw_udc *udc = the_controller;

	switch (cmd)
	{
		case SW_UDC_P_ENABLE:
		{
        	if(udc->driver){
        			usbd_start_work();
            }else{
                DMSG_INFO("udc->driver is null, udc is need not start\n");
            }
        }
		break;

		case SW_UDC_P_DISABLE:
		{
        	if(udc->driver){
        			usbd_stop_work();
            }else{
                DMSG_INFO("udc->driver is null, udc is need not stop\n");
            }
        }
		break;

		case SW_UDC_P_RESET :
			DMSG_PANIC("ERR: reset is not support\n");
		break;

		default:
			DMSG_PANIC("ERR: unkown cmd(%d)\n",cmd);
			break;
	}

	return ;
}

static void cfg_vbus_draw(unsigned int ma)
{
	return;
}

/*
*******************************************************************************
*                     udc_init
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int __init udc_init(void)
{
	int retval = 0;

	DMSG_INFO_UDC("udc_init: version %s\n", DRIVER_VERSION);

    usb_connect = 0;

	atomic_set(&vfs_read_flag, 0);
	atomic_set(&vfs_write_flag, 0);

	atomic_set(&ep_num_dma_rx, 0);

    /* driver register */
	retval = platform_driver_probe(&sw_udc_driver, sw_udc_probe);
	if(retval){
        DMSG_PANIC("ERR: platform_driver_register failed\n");
        retval = -1;
		goto err;
    }

	return 0;

err:
	return retval;
}

/*
*******************************************************************************
*                     udc_exit
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void __exit udc_exit(void)
{
	DMSG_INFO_UDC("udc_exit: version %s\n", DRIVER_VERSION);

	platform_driver_unregister(&sw_udc_driver);

	return ;
}

//module_init(udc_init);
fs_initcall(udc_init);
module_exit(udc_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:softwinner-usbgadget");


