/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2017-10-10     Tanek        the first version
 * 2019-5-10      misonyo      add DMA TX and RX function
 */

#include <rtthread.h>
#include "board.h"
#include <rtdevice.h>

#ifdef RT_USING_FINSH
    #include <finsh.h>
#endif

#include "fsl_enet.h"
#include "fsl_gpio.h"
#include "fsl_phy.h"
#include "fsl_cache.h"
#include "fsl_iomuxc.h"

#ifdef RT_USING_LWIP

#include <netif/ethernetif.h>
#include "lwipopts.h"

#define ENET_RXBD_NUM (4)
#define ENET_TXBD_NUM (4)
#define ENET_RXBUFF_SIZE (ENET_FRAME_MAX_FRAMELEN)
#define ENET_TXBUFF_SIZE (ENET_FRAME_MAX_FRAMELEN)

/* debug option */
#undef ETH_RX_DUMP
#undef ETH_TX_DUMP

#define DBG_ENABLE
#define DBG_SECTION_NAME    "[ETH]"
#define DBG_COLOR
#define DBG_LEVEL           DBG_INFO
#include <rtdbg.h>

#define MAX_ADDR_LEN 6

struct rt_imxrt_eth
{
    /* inherit from ethernet device */
    struct eth_device parent;

    enet_handle_t enet_handle;
    ENET_Type *enet_base;
    enet_data_error_stats_t error_statistic;
    rt_uint8_t  dev_addr[MAX_ADDR_LEN];         /* hw address   */

    rt_bool_t tx_is_waiting;
    struct rt_semaphore tx_wait;

    enet_mii_speed_t speed;
    enet_mii_duplex_t duplex;
};

ALIGN(ENET_BUFF_ALIGNMENT) enet_tx_bd_struct_t g_txBuffDescrip[ENET_TXBD_NUM] SECTION("NonCacheable");
ALIGN(ENET_BUFF_ALIGNMENT) rt_uint8_t g_txDataBuff[ENET_TXBD_NUM][RT_ALIGN(ENET_TXBUFF_SIZE, ENET_BUFF_ALIGNMENT)];

ALIGN(ENET_BUFF_ALIGNMENT) enet_rx_bd_struct_t g_rxBuffDescrip[ENET_RXBD_NUM] SECTION("NonCacheable");
ALIGN(ENET_BUFF_ALIGNMENT) rt_uint8_t g_rxDataBuff[ENET_RXBD_NUM][RT_ALIGN(ENET_RXBUFF_SIZE, ENET_BUFF_ALIGNMENT)];

static struct rt_imxrt_eth imxrt_eth_device;

void _enet_rx_callback(struct rt_imxrt_eth *eth)
{
    rt_err_t result;

    ENET_DisableInterrupts(eth->enet_base, kENET_RxFrameInterrupt);

    result = eth_device_ready(&(eth->parent));
    if (result != RT_EOK)
        rt_kprintf("RX err =%d\n", result);
}

void _enet_tx_callback(struct rt_imxrt_eth *eth)
{
    if (eth->tx_is_waiting == RT_TRUE)
    {
        eth->tx_is_waiting = RT_FALSE;
        rt_sem_release(&eth->tx_wait);
    }
}

void _enet_callback(ENET_Type *base, enet_handle_t *handle, enet_event_t event, void *userData)
{
    switch (event)
    {
    case kENET_RxEvent:

        _enet_rx_callback((struct rt_imxrt_eth *)userData);
        break;

    case kENET_TxEvent:
        _enet_tx_callback((struct rt_imxrt_eth *)userData);
        break;

    case kENET_ErrEvent:
        dbg_log(DBG_LOG, "kENET_ErrEvent\n");
        break;

    case kENET_WakeUpEvent:
        dbg_log(DBG_LOG, "kENET_WakeUpEvent\n");
        break;

    case kENET_TimeStampEvent:
        dbg_log(DBG_LOG, "kENET_TimeStampEvent\n");
        break;

    case kENET_TimeStampAvailEvent:
        dbg_log(DBG_LOG, "kENET_TimeStampAvailEvent \n");
        break;

    default:
        dbg_log(DBG_LOG, "unknow error\n");
        break;
    }
}

static void _enet_clk_init(void)
{
    const clock_enet_pll_config_t config = {.enableClkOutput = true, .enableClkOutput25M = false, .loopDivider = 1};
    CLOCK_InitEnetPll(&config);

    IOMUXC_EnableMode(IOMUXC_GPR, kIOMUXC_GPR_ENET1TxClkOutputDir, true);
    IOMUXC_GPR->GPR1|=1<<23;
}

static void _enet_config(void)
{
    enet_config_t config;
    uint32_t sysClock;

    /* prepare the buffer configuration. */
    enet_buffer_config_t buffConfig =
    {
        ENET_RXBD_NUM,
        ENET_TXBD_NUM,
        SDK_SIZEALIGN(ENET_RXBUFF_SIZE, ENET_BUFF_ALIGNMENT),
        SDK_SIZEALIGN(ENET_TXBUFF_SIZE, ENET_BUFF_ALIGNMENT),
        &g_rxBuffDescrip[0],
        &g_txBuffDescrip[0],
        &g_rxDataBuff[0][0],
        &g_txDataBuff[0][0],
    };

    /* Get default configuration. */
    /*
     * config.miiMode = kENET_RmiiMode;
     * config.miiSpeed = kENET_MiiSpeed100M;
     * config.miiDuplex = kENET_MiiFullDuplex;
     * config.rxMaxFrameLen = ENET_FRAME_MAX_FRAMELEN;
     */
    ENET_GetDefaultConfig(&config);
    config.interrupt = kENET_TxFrameInterrupt | kENET_RxFrameInterrupt;
    config.miiSpeed = imxrt_eth_device.speed;
    config.miiDuplex = imxrt_eth_device.duplex;

    /* Set SMI to get PHY link status. */
    sysClock = CLOCK_GetFreq(kCLOCK_AhbClk);

    dbg_log(DBG_LOG, "deinit\n");
    ENET_Deinit(imxrt_eth_device.enet_base);
    dbg_log(DBG_LOG, "init\n");
    ENET_Init(imxrt_eth_device.enet_base, &imxrt_eth_device.enet_handle, &config, &buffConfig, &imxrt_eth_device.dev_addr[0], sysClock);
    dbg_log(DBG_LOG, "set call back\n");
    ENET_SetCallback(&imxrt_eth_device.enet_handle, _enet_callback, &imxrt_eth_device);
    dbg_log(DBG_LOG, "active read\n");
    ENET_ActiveRead(imxrt_eth_device.enet_base);
}

#if defined(ETH_RX_DUMP) ||  defined(ETH_TX_DUMP)
static void packet_dump(const char *msg, const struct pbuf *p)
{
    const struct pbuf *q;
    rt_uint32_t i, j;
    rt_uint8_t *ptr;

    rt_kprintf("%s %d byte\n", msg, p->tot_len);

    i = 0;
    for (q = p; q != RT_NULL; q = q->next)
    {
        ptr = q->payload;

        for (j = 0; j < q->len; j++)
        {
            if ((i % 8) == 0)
            {
                rt_kprintf("  ");
            }
            if ((i % 16) == 0)
            {
                rt_kprintf("\r\n");
            }
            rt_kprintf("%02x ", *ptr);

            i++;
            ptr++;
        }
    }

    rt_kprintf("\n\n");
}
#else
#define packet_dump(...)
#endif /* dump */

/* initialize the interface */
static rt_err_t rt_imxrt_eth_init(rt_device_t dev)
{
    dbg_log(DBG_LOG, "rt_imxrt_eth_init...\n");
    _enet_config();

    return RT_EOK;
}

static rt_err_t rt_imxrt_eth_open(rt_device_t dev, rt_uint16_t oflag)
{
    dbg_log(DBG_LOG, "rt_imxrt_eth_open...\n");
    return RT_EOK;
}

static rt_err_t rt_imxrt_eth_close(rt_device_t dev)
{
    dbg_log(DBG_LOG, "rt_imxrt_eth_close...\n");
    return RT_EOK;
}

static rt_size_t rt_imxrt_eth_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    dbg_log(DBG_LOG, "rt_imxrt_eth_read...\n");
    rt_set_errno(-RT_ENOSYS);
    return 0;
}

static rt_size_t rt_imxrt_eth_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    dbg_log(DBG_LOG, "rt_imxrt_eth_write...\n");
    rt_set_errno(-RT_ENOSYS);
    return 0;
}

static rt_err_t rt_imxrt_eth_control(rt_device_t dev, int cmd, void *args)
{
    dbg_log(DBG_LOG, "rt_imxrt_eth_control...\n");
    switch (cmd)
    {
    case NIOCTL_GADDR:
        /* get mac address */
        if (args) rt_memcpy(args, imxrt_eth_device.dev_addr, 6);
        else return -RT_ERROR;
        break;

    default :
        break;
    }

    return RT_EOK;
}

static void _ENET_ActiveSend(ENET_Type *base, uint32_t ringId)
{
    assert(ringId < FSL_FEATURE_ENET_QUEUE);

    switch (ringId)
    {
        case 0:
            base->TDAR = ENET_TDAR_TDAR_MASK;
            break;
#if FSL_FEATURE_ENET_QUEUE > 1
        case kENET_Ring1:
            base->TDAR1 = ENET_TDAR1_TDAR_MASK;
            break;
        case kENET_Ring2:
            base->TDAR2 = ENET_TDAR2_TDAR_MASK;
            break;
#endif /* FSL_FEATURE_ENET_QUEUE > 1 */
        default:
            base->TDAR = ENET_TDAR_TDAR_MASK;
            break;
    }
}

static status_t _ENET_SendFrame(ENET_Type *base, enet_handle_t *handle, const uint8_t *data, uint32_t length)
{
    assert(handle);
    assert(data);

    volatile enet_tx_bd_struct_t *curBuffDescrip;
    uint32_t len = 0;
    uint32_t sizeleft = 0;
    uint32_t address;

    /* Check the frame length. */
    if (length > ENET_FRAME_MAX_FRAMELEN)
    {
        return kStatus_ENET_TxFrameOverLen;
    }

    /* Check if the transmit buffer is ready. */
    curBuffDescrip = handle->txBdCurrent[0];
    if (curBuffDescrip->control & ENET_BUFFDESCRIPTOR_TX_READY_MASK)
    {
        return kStatus_ENET_TxFrameBusy;
    }
#ifdef ENET_ENHANCEDBUFFERDESCRIPTOR_MODE
    bool isPtpEventMessage = false;
    /* Check PTP message with the PTP header. */
    isPtpEventMessage = ENET_Ptp1588ParseFrame(data, NULL, true);
#endif /* ENET_ENHANCEDBUFFERDESCRIPTOR_MODE */
    /* One transmit buffer is enough for one frame. */
    if (handle->txBuffSizeAlign[0] >= length)
    {
        /* Copy data to the buffer for uDMA transfer. */
#if defined(FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET) && FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET
        address = MEMORY_ConvertMemoryMapAddress((uint32_t)curBuffDescrip->buffer,kMEMORY_DMA2Local);
#else
        address = (uint32_t)curBuffDescrip->buffer;
#endif /* FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET */

        pbuf_copy_partial((const struct pbuf *)data, (void *)address, length, 0);
            
        /* Set data length. */
        curBuffDescrip->length = length;
#ifdef ENET_ENHANCEDBUFFERDESCRIPTOR_MODE
        /* For enable the timestamp. */
        if (isPtpEventMessage)
        {
            curBuffDescrip->controlExtend1 |= ENET_BUFFDESCRIPTOR_TX_TIMESTAMP_MASK;
        }
        else
        {
            curBuffDescrip->controlExtend1 &= ~ENET_BUFFDESCRIPTOR_TX_TIMESTAMP_MASK;
        }

#endif /* ENET_ENHANCEDBUFFERDESCRIPTOR_MODE */
        curBuffDescrip->control |= (ENET_BUFFDESCRIPTOR_TX_READY_MASK | ENET_BUFFDESCRIPTOR_TX_LAST_MASK);

        /* Increase the buffer descriptor address. */
        if (curBuffDescrip->control & ENET_BUFFDESCRIPTOR_TX_WRAP_MASK)
        {
            handle->txBdCurrent[0] = handle->txBdBase[0];
        }
        else
        {
            handle->txBdCurrent[0]++;
        }
#if defined(FSL_SDK_ENABLE_DRIVER_CACHE_CONTROL) && FSL_SDK_ENABLE_DRIVER_CACHE_CONTROL
        /* Add the cache clean maintain. */
#if defined(FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET) && FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET
        address = MEMORY_ConvertMemoryMapAddress((uint32_t)curBuffDescrip->buffer,kMEMORY_DMA2Local);
#else
        address = (uint32_t)curBuffDescrip->buffer;
#endif /* FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET */
        DCACHE_CleanByRange(address, length);
#endif  /* FSL_SDK_ENABLE_DRIVER_CACHE_CONTROL */
        /* Active the transmit buffer descriptor. */
        _ENET_ActiveSend(base, 0);

        return kStatus_Success;
    }
    else
    {
        /* One frame requires more than one transmit buffers. */
        do
        {
#ifdef ENET_ENHANCEDBUFFERDESCRIPTOR_MODE
            /* For enable the timestamp. */
            if (isPtpEventMessage)
            {
                curBuffDescrip->controlExtend1 |= ENET_BUFFDESCRIPTOR_TX_TIMESTAMP_MASK;
            }
            else
            {
                curBuffDescrip->controlExtend1 &= ~ENET_BUFFDESCRIPTOR_TX_TIMESTAMP_MASK;
            }
#endif /* ENET_ENHANCEDBUFFERDESCRIPTOR_MODE */

            /* Increase the buffer descriptor address. */
            if (curBuffDescrip->control & ENET_BUFFDESCRIPTOR_TX_WRAP_MASK)
            {
                handle->txBdCurrent[0] = handle->txBdBase[0];
            }
            else
            {
                handle->txBdCurrent[0]++;
            }
            /* update the size left to be transmit. */
            sizeleft = length - len;
            if (sizeleft > handle->txBuffSizeAlign[0])
            {
                /* Data copy. */
#if defined(FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET) && FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET
                address = MEMORY_ConvertMemoryMapAddress((uint32_t)curBuffDescrip->buffer,kMEMORY_DMA2Local);
#else
                address = (uint32_t)curBuffDescrip->buffer;
#endif /* FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET */
                memcpy((void *)address, data + len, handle->txBuffSizeAlign[0]);
                /* Data length update. */
                curBuffDescrip->length = handle->txBuffSizeAlign[0];
                len += handle->txBuffSizeAlign[0];
                /* Sets the control flag. */
                curBuffDescrip->control &= ~ENET_BUFFDESCRIPTOR_TX_LAST_MASK;
                curBuffDescrip->control |= ENET_BUFFDESCRIPTOR_TX_READY_MASK;
                /* Active the transmit buffer descriptor*/
                _ENET_ActiveSend(base, 0);
            }
            else
            {
#if defined(FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET) && FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET
                address = MEMORY_ConvertMemoryMapAddress((uint32_t)curBuffDescrip->buffer,kMEMORY_DMA2Local);
#else
                address = (uint32_t)curBuffDescrip->buffer;
#endif /* FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET */
                memcpy((void *)address, data + len, sizeleft);
                curBuffDescrip->length = sizeleft;
                /* Set Last buffer wrap flag. */
                curBuffDescrip->control |= ENET_BUFFDESCRIPTOR_TX_READY_MASK | ENET_BUFFDESCRIPTOR_TX_LAST_MASK;
#if defined(FSL_SDK_ENABLE_DRIVER_CACHE_CONTROL) && FSL_SDK_ENABLE_DRIVER_CACHE_CONTROL
                /* Add the cache clean maintain. */
#if defined(FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET) && FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET
                address = MEMORY_ConvertMemoryMapAddress((uint32_t)curBuffDescrip->buffer,kMEMORY_DMA2Local);
#else
                address = (uint32_t)curBuffDescrip->buffer;
#endif /* FSL_FEATURE_MEMORY_HAS_ADDRESS_OFFSET */
                DCACHE_CleanByRange(address, handle->txBuffSizeAlign[0]);
#endif  /* FSL_SDK_ENABLE_DRIVER_CACHE_CONTROL */                 
                /* Active the transmit buffer descriptor. */
                _ENET_ActiveSend(base, 0);

                return kStatus_Success;
            }

            /* Get the current buffer descriptor address. */
            curBuffDescrip = handle->txBdCurrent[0];

        } while (!(curBuffDescrip->control & ENET_BUFFDESCRIPTOR_TX_READY_MASK));

        return kStatus_ENET_TxFrameBusy;
    }
}

/* ethernet device interface */
/* transmit packet. */
rt_err_t rt_imxrt_eth_tx(rt_device_t dev, struct pbuf *p)
{
	rt_err_t result = RT_EOK;
	enet_handle_t * enet_handle = &imxrt_eth_device.enet_handle;

    RT_ASSERT(p != NULL);
    RT_ASSERT(enet_handle != RT_NULL);

    dbg_log(DBG_LOG, "rt_imxrt_eth_tx: %d\n", p->len);

#ifdef ETH_TX_DUMP
    packet_dump("send", p);
#endif

    do
    {
        result = _ENET_SendFrame(imxrt_eth_device.enet_base, enet_handle, (const uint8_t *)p, p->tot_len);

        if (result == kStatus_ENET_TxFrameBusy)
        {
            imxrt_eth_device.tx_is_waiting = RT_TRUE;
            rt_sem_take(&imxrt_eth_device.tx_wait, RT_WAITING_FOREVER);
        }

    }
    while (result == kStatus_ENET_TxFrameBusy);

    return RT_EOK;
}

/* reception packet. */
struct pbuf *rt_imxrt_eth_rx(rt_device_t dev)
{
    uint32_t length = 0;
    status_t status;

    struct pbuf *p = RT_NULL;
    enet_handle_t *enet_handle = &imxrt_eth_device.enet_handle;
    ENET_Type *enet_base = imxrt_eth_device.enet_base;
    enet_data_error_stats_t *error_statistic = &imxrt_eth_device.error_statistic;

    /* Get the Frame size */
    status = ENET_GetRxFrameSize(enet_handle, &length);

    /* Call ENET_ReadFrame when there is a received frame. */
    if (length != 0)
    {
        /* Received valid frame. Deliver the rx buffer with the size equal to length. */
        p = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);

        if (p != NULL)
        {
            status = ENET_ReadFrame(enet_base, enet_handle, p->payload, length);
            if (status == kStatus_Success)
            {
#ifdef ETH_RX_DUMP
                packet_dump("recv", p);
#endif
                return p;
            }
            else
            {
                dbg_log(DBG_LOG, " A frame read failed\n");
                pbuf_free(p);
            }
        }
        else
        {
            dbg_log(DBG_LOG, " pbuf_alloc faild\n");
        }
    }
    else if (status == kStatus_ENET_RxFrameError)
    {
        dbg_log(DBG_WARNING, "ENET_GetRxFrameSize: kStatus_ENET_RxFrameError\n");
        /* Update the received buffer when error happened. */
        /* Get the error information of the received g_frame. */
        ENET_GetRxErrBeforeReadFrame(enet_handle, error_statistic);
        /* update the receive buffer. */
        ENET_ReadFrame(enet_base, enet_handle, NULL, 0);
    }

    ENET_EnableInterrupts(enet_base, kENET_RxFrameInterrupt);
    return NULL;
}

static void phy_monitor_thread_entry(void *parameter)
{
    phy_speed_t speed;
    phy_duplex_t duplex;
    bool link = false;

    imxrt_enet_phy_reset_by_gpio();

    PHY_Init(imxrt_eth_device.enet_base, PHY_ADDRESS, CLOCK_GetFreq(kCLOCK_AhbClk));

    while (1)
    {
        bool new_link = false;
        status_t status = PHY_GetLinkStatus(imxrt_eth_device.enet_base, PHY_ADDRESS, &new_link);

        if ((status == kStatus_Success) && (link != new_link))
        {
            link = new_link;

            if (link)   // link up
            {
                PHY_GetLinkSpeedDuplex(imxrt_eth_device.enet_base,
                                       PHY_ADDRESS, &speed, &duplex);

                if (kPHY_Speed10M == speed)
                {
                    dbg_log(DBG_LOG, "10M\n");
                }
                else
                {
                    dbg_log(DBG_LOG, "100M\n");
                }

                if (kPHY_HalfDuplex == duplex)
                {
                    dbg_log(DBG_LOG, "half dumplex\n");
                }
                else
                {
                    dbg_log(DBG_LOG, "full dumplex\n");
                }

                if ((imxrt_eth_device.speed != (enet_mii_speed_t)speed)
                        || (imxrt_eth_device.duplex != (enet_mii_duplex_t)duplex))
                {
                    imxrt_eth_device.speed = (enet_mii_speed_t)speed;
                    imxrt_eth_device.duplex = (enet_mii_duplex_t)duplex;

                    dbg_log(DBG_LOG, "link up, and update eth mode.\n");
                    rt_imxrt_eth_init((rt_device_t)&imxrt_eth_device);
                }
                else
                {
                    dbg_log(DBG_LOG, "link up, eth not need re-config.\n");
                }
                dbg_log(DBG_LOG, "link up.\n");
                eth_device_linkchange(&imxrt_eth_device.parent, RT_TRUE);
            }
            else
            {
                dbg_log(DBG_LOG, "link down.\n");
                eth_device_linkchange(&imxrt_eth_device.parent, RT_FALSE);
            }
        }

        rt_thread_delay(RT_TICK_PER_SECOND * 2);
    }
}

static int rt_hw_imxrt_eth_init(void)
{
    rt_err_t state;

    _enet_clk_init();

    /* NXP (Freescale) MAC OUI */
    imxrt_eth_device.dev_addr[0] = 0x00;
    imxrt_eth_device.dev_addr[1] = 0x04;
    imxrt_eth_device.dev_addr[2] = 0x9F;
    /* generate MAC addr from 96bit unique ID (only for test). */
    imxrt_eth_device.dev_addr[3] = 0x05;
    imxrt_eth_device.dev_addr[4] = 0x44;
    imxrt_eth_device.dev_addr[5] = 0xE5;

    imxrt_eth_device.speed = kENET_MiiSpeed100M;
    imxrt_eth_device.duplex = kENET_MiiFullDuplex;

    imxrt_eth_device.enet_base = ENET;

    imxrt_eth_device.parent.parent.init       = rt_imxrt_eth_init;
    imxrt_eth_device.parent.parent.open       = rt_imxrt_eth_open;
    imxrt_eth_device.parent.parent.close      = rt_imxrt_eth_close;
    imxrt_eth_device.parent.parent.read       = rt_imxrt_eth_read;
    imxrt_eth_device.parent.parent.write      = rt_imxrt_eth_write;
    imxrt_eth_device.parent.parent.control    = rt_imxrt_eth_control;
    imxrt_eth_device.parent.parent.user_data  = RT_NULL;

    imxrt_eth_device.parent.eth_rx     = rt_imxrt_eth_rx;
    imxrt_eth_device.parent.eth_tx     = rt_imxrt_eth_tx;

    dbg_log(DBG_LOG, "sem init: tx_wait\r\n");
    /* init tx semaphore */
    rt_sem_init(&imxrt_eth_device.tx_wait, "tx_wait", 0, RT_IPC_FLAG_FIFO);

    /* register eth device */
    dbg_log(DBG_LOG, "eth_device_init start\r\n");
    state = eth_device_init(&(imxrt_eth_device.parent), "e0");
    if (RT_EOK == state)
    {
        dbg_log(DBG_LOG, "eth_device_init success\r\n");
    }
    else
    {
        dbg_log(DBG_LOG, "eth_device_init faild: %d\r\n", state);
    }

    eth_device_linkchange(&imxrt_eth_device.parent, RT_FALSE);

    /* start phy monitor */
    {
        rt_thread_t tid;
        tid = rt_thread_create("phy",
                               phy_monitor_thread_entry,
                               RT_NULL,
                               512,
                               RT_THREAD_PRIORITY_MAX - 2,
                               2);
        if (tid != RT_NULL)
            rt_thread_startup(tid);
    }

    return state;
}
INIT_DEVICE_EXPORT(rt_hw_imxrt_eth_init);
#endif

#ifdef RT_USING_FINSH
#include <finsh.h>

void phy_read(uint32_t phyReg)
{
    uint32_t data;
    status_t status;

    status = PHY_Read(imxrt_eth_device.enet_base, PHY_ADDRESS, phyReg, &data);
    if (kStatus_Success == status)
    {
        rt_kprintf("PHY_Read: %02X --> %08X", phyReg, data);
    }
    else
    {
        rt_kprintf("PHY_Read: %02X --> faild", phyReg);
    }
}

void phy_write(uint32_t phyReg, uint32_t data)
{
    status_t status;

    status = PHY_Write(imxrt_eth_device.enet_base, PHY_ADDRESS, phyReg, data);
    if (kStatus_Success == status)
    {
        rt_kprintf("PHY_Write: %02X --> %08X\n", phyReg, data);
    }
    else
    {
        rt_kprintf("PHY_Write: %02X --> faild\n", phyReg);
    }
}

void phy_dump(void)
{
    uint32_t data;
    status_t status;

    int i;
    for (i = 0; i < 32; i++)
    {
        status = PHY_Read(imxrt_eth_device.enet_base, PHY_ADDRESS, i, &data);
        if (kStatus_Success != status)
        {
            rt_kprintf("phy_dump: %02X --> faild", i);
            break;
        }

        if (i % 8 == 7)
        {
            rt_kprintf("%02X --> %08X ", i, data);
        }
        else
        {
            rt_kprintf("%02X --> %08X\n", i, data);
        }

    }
}

void enet_reg_dump(void)
{
    ENET_Type *enet_base = imxrt_eth_device.enet_base;

#define DUMP_REG(__REG)  \
    rt_kprintf("%s(%08X): %08X\n", #__REG, (uint32_t)&enet_base->__REG, enet_base->__REG)

    DUMP_REG(EIR);
    DUMP_REG(EIMR);
    DUMP_REG(RDAR);
    DUMP_REG(TDAR);
    DUMP_REG(ECR);
    DUMP_REG(MMFR);
    DUMP_REG(MSCR);
    DUMP_REG(MIBC);
    DUMP_REG(RCR);
    DUMP_REG(TCR);
    DUMP_REG(PALR);
    DUMP_REG(PAUR);
    DUMP_REG(OPD);
    DUMP_REG(TXIC);
    DUMP_REG(RXIC);
    DUMP_REG(IAUR);
    DUMP_REG(IALR);
    DUMP_REG(GAUR);
    DUMP_REG(GALR);
    DUMP_REG(TFWR);
    DUMP_REG(RDSR);
    DUMP_REG(TDSR);
    DUMP_REG(MRBR);
    DUMP_REG(RSFL);
    DUMP_REG(RSEM);
    DUMP_REG(RAEM);
    DUMP_REG(RAFL);
    DUMP_REG(TSEM);
    DUMP_REG(TAEM);
    DUMP_REG(TAFL);
    DUMP_REG(TIPG);
    DUMP_REG(FTRL);
    DUMP_REG(TACC);
    DUMP_REG(RACC);
    DUMP_REG(RMON_T_DROP);
    DUMP_REG(RMON_T_PACKETS);
    DUMP_REG(RMON_T_BC_PKT);
    DUMP_REG(RMON_T_MC_PKT);
    DUMP_REG(RMON_T_CRC_ALIGN);
    DUMP_REG(RMON_T_UNDERSIZE);
    DUMP_REG(RMON_T_OVERSIZE);
    DUMP_REG(RMON_T_FRAG);
    DUMP_REG(RMON_T_JAB);
    DUMP_REG(RMON_T_COL);
    DUMP_REG(RMON_T_P64);
    DUMP_REG(RMON_T_P65TO127);
    DUMP_REG(RMON_T_P128TO255);
    DUMP_REG(RMON_T_P256TO511);
    DUMP_REG(RMON_T_P512TO1023);
    DUMP_REG(RMON_T_P1024TO2047);
    DUMP_REG(RMON_T_P_GTE2048);
    DUMP_REG(RMON_T_OCTETS);
    DUMP_REG(IEEE_T_DROP);
    DUMP_REG(IEEE_T_FRAME_OK);
    DUMP_REG(IEEE_T_1COL);
    DUMP_REG(IEEE_T_MCOL);
    DUMP_REG(IEEE_T_DEF);
    DUMP_REG(IEEE_T_LCOL);
    DUMP_REG(IEEE_T_EXCOL);
    DUMP_REG(IEEE_T_MACERR);
    DUMP_REG(IEEE_T_CSERR);
    DUMP_REG(IEEE_T_SQE);
    DUMP_REG(IEEE_T_FDXFC);
    DUMP_REG(IEEE_T_OCTETS_OK);
    DUMP_REG(RMON_R_PACKETS);
    DUMP_REG(RMON_R_BC_PKT);
    DUMP_REG(RMON_R_MC_PKT);
    DUMP_REG(RMON_R_CRC_ALIGN);
    DUMP_REG(RMON_R_UNDERSIZE);
    DUMP_REG(RMON_R_OVERSIZE);
    DUMP_REG(RMON_R_FRAG);
    DUMP_REG(RMON_R_JAB);
    DUMP_REG(RMON_R_RESVD_0);
    DUMP_REG(RMON_R_P64);
    DUMP_REG(RMON_R_P65TO127);
    DUMP_REG(RMON_R_P128TO255);
    DUMP_REG(RMON_R_P256TO511);
    DUMP_REG(RMON_R_P512TO1023);
    DUMP_REG(RMON_R_P1024TO2047);
    DUMP_REG(RMON_R_P_GTE2048);
    DUMP_REG(RMON_R_OCTETS);
    DUMP_REG(IEEE_R_DROP);
    DUMP_REG(IEEE_R_FRAME_OK);
    DUMP_REG(IEEE_R_CRC);
    DUMP_REG(IEEE_R_ALIGN);
    DUMP_REG(IEEE_R_MACERR);
    DUMP_REG(IEEE_R_FDXFC);
    DUMP_REG(IEEE_R_OCTETS_OK);
    DUMP_REG(ATCR);
    DUMP_REG(ATVR);
    DUMP_REG(ATOFF);
    DUMP_REG(ATPER);
    DUMP_REG(ATCOR);
    DUMP_REG(ATINC);
    DUMP_REG(ATSTMP);
    DUMP_REG(TGSR);
}

void enet_nvic_tog(void)
{
    NVIC_SetPendingIRQ(ENET_IRQn);
}

void enet_rx_stat(void)
{
    enet_data_error_stats_t *error_statistic = &imxrt_eth_device.error_statistic;

#define DUMP_STAT(__VAR)  \
    rt_kprintf("%-25s: %08X\n", #__VAR, error_statistic->__VAR);

    DUMP_STAT(statsRxLenGreaterErr);
    DUMP_STAT(statsRxAlignErr);
    DUMP_STAT(statsRxFcsErr);
    DUMP_STAT(statsRxOverRunErr);
    DUMP_STAT(statsRxTruncateErr);

#ifdef ENET_ENHANCEDBUFFERDESCRIPTOR_MODE
    DUMP_STAT(statsRxProtocolChecksumErr);
    DUMP_STAT(statsRxIpHeadChecksumErr);
    DUMP_STAT(statsRxMacErr);
    DUMP_STAT(statsRxPhyErr);
    DUMP_STAT(statsRxCollisionErr);
    DUMP_STAT(statsTxErr);
    DUMP_STAT(statsTxFrameErr);
    DUMP_STAT(statsTxOverFlowErr);
    DUMP_STAT(statsTxLateCollisionErr);
    DUMP_STAT(statsTxExcessCollisionErr);
    DUMP_STAT(statsTxUnderFlowErr);
    DUMP_STAT(statsTxTsErr);
#endif

}

void enet_buf_info(void)
{
    int i = 0;
    for (i = 0; i < ENET_RXBD_NUM; i++)
    {
        rt_kprintf("%d: length: %-8d, control: %04X, buffer:%p\n",
                   i,
                   g_rxBuffDescrip[i].length,
                   g_rxBuffDescrip[i].control,
                   g_rxBuffDescrip[i].buffer);
    }

    for (i = 0; i < ENET_TXBD_NUM; i++)
    {
        rt_kprintf("%d: length: %-8d, control: %04X, buffer:%p\n",
                   i,
                   g_txBuffDescrip[i].length,
                   g_txBuffDescrip[i].control,
                   g_txBuffDescrip[i].buffer);
    }
}

FINSH_FUNCTION_EXPORT(phy_read, read phy register);
FINSH_FUNCTION_EXPORT(phy_write, write phy register);
FINSH_FUNCTION_EXPORT(phy_dump, dump phy registers);
FINSH_FUNCTION_EXPORT(enet_reg_dump, dump enet registers);
FINSH_FUNCTION_EXPORT(enet_nvic_tog, toggle enet nvic pendding bit);
FINSH_FUNCTION_EXPORT(enet_rx_stat, dump enet rx statistic);
FINSH_FUNCTION_EXPORT(enet_buf_info, dump enet tx and tx buffer descripter);

#endif
