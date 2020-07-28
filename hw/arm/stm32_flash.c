/*
 * STM32 Microcontroller Flash Memory
 * The STM32 family stores its Flash memory at some base address in memory
 * (0x08000000 for medium density devices), and then aliases it to the
 * boot memory space, which starts at 0x00000000 (the System Memory can also
 * be aliased to 0x00000000, but this is not implemented here).  The processor
 * executes the code in the aliased memory at 0x00000000, but we need to
 * implement the "real" flash memory as well.  This "real" flash memory will
 * pass reads through to the memory at 0x00000000, which is where QEMU loads
 * the executable image.  Note that this is opposite of real hardware, where the
 * memory at 0x00000000 passes reads through the "real" flash memory, but it
 * works the same either way.
 *
 * Copyright (C) 2010 Andre Beckus
 *
 * Implementation based on ST Microelectronics "RM0008 Reference Manual Rev 10"
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/arm/stm32.h"

#include "sysemu/blockdev.h"
#include "hw/hw.h"
#include "hw/block/flash.h"
#include "block/block.h"
#include "hw/sysbus.h"


typedef struct Stm32Flash {
	SysBusDevice busdev;   
    BlockDriverState * blks;
    hwaddr base_address;
	uint32_t size;
    MemoryRegion iomem;
    void *data;
    hwaddr SP_init;
    hwaddr PC_init;    
} Stm32Flash;

static Stm32Flash*flash;
static uint32_t is_flash_locked = 1;
static uint32_t flash_programming_bit = 0;


/* */
Stm32Flash *stm32_flash_register(BlockDriverState *blks, hwaddr base,
                                  hwaddr size)
{
    DeviceState *dev = qdev_create(NULL, "stm32-flash");
    Stm32Flash *flash = (Stm32Flash *)object_dynamic_cast(OBJECT(dev),"stm32-flash");
    qdev_prop_set_uint32(dev, "size", size);
    qdev_prop_set_uint64(dev, "base_address", base);
    if (blks) {
    	int err = 0;
        err = qdev_prop_set_drive(dev, "drive", blks);
        if (err) {
            printf("%s, have no drive???\n", __func__);
            return NULL;
        }
    }
    qdev_init_nofail(dev);   
    //sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
  
    return flash;
}


MemoryRegion *get_system_memory(void); /* XXX */

static int stm32_flash_init(SysBusDevice *dev)
{    
    flash = DO_UPCAST(Stm32Flash, busdev, dev);

//    memory_region_init_rom_device(&flash->mem, &f2xx_flash_ops, flash, "name",
//      size);
    memory_region_init_ram(&flash->iomem, NULL, "f2xx.flash", flash->size);

    vmstate_register_ram(&flash->iomem, DEVICE(flash));
    //vmstate_register_ram_global(&flash->iomem);
    memory_region_set_readonly(&flash->iomem, true);
    memory_region_add_subregion(get_system_memory(), flash->base_address, &flash->iomem);


    flash->data = memory_region_get_ram_ptr(&flash->iomem);
    memset(flash->data, 0xff, flash->size);
    if (flash->blks) {
        int r;
        r = bdrv_read(flash->blks, 0, flash->data, bdrv_getlength(flash->blks)/BDRV_SECTOR_SIZE);
        if (r < 0) {
            vmstate_unregister_ram(&flash->iomem, DEVICE(flash));
            // memory_region_destroy(&flash->mem);
            return 1;
        }
        else{
          uint32_t * mem = flash->data; 
          flash->PC_init = mem[1]; 
          flash->SP_init = mem[0];   
        }
    }

    return 0;
}

static void
stm32_flash_reset(DeviceState *ds)
{
	Stm32Flash *s = STM32_FLASH(ds);

	printf("reset is called!\n\n");
   
    //ARM M3 FLASH reset
    ARMCPU *cpu = ARM_CPU(qemu_get_cpu(0));
    CPUARMState *env = &cpu->env; 
   
    env->regs[13] = s->SP_init & 0xFFFFFFFC;
    env->thumb = s->PC_init & 1;
    env->regs[15] = s->PC_init & ~1;
    
}

static Property stm32_flash_properties[] = {
    DEFINE_PROP_DRIVE("drive", Stm32Flash, blks),
    DEFINE_PROP_UINT32("size", Stm32Flash, size, 0),
    DEFINE_PROP_UINT64("base_address", Stm32Flash , base_address, STM32_FLASH_ADDR_START),
    DEFINE_PROP_END_OF_LIST(),
};


static void stm32_flash_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

	k->init = stm32_flash_init;
	dc->props = stm32_flash_properties;
    dc->reset = stm32_flash_reset;
}

static TypeInfo stm32_flash_info = {
	.name          = "stm32-flash",
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(Stm32Flash),
	.class_init    = stm32_flash_class_init,
};


static void stm32_flash_register_types(void)
{
	type_register_static(&stm32_flash_info);
}

type_init(stm32_flash_register_types);


//////regs


#define R_FLASH_ACR            (0x00 / 4)
#define R_FLASH_KEYR           (0x04 / 4)
#define R_FLASH_OPTKEYR        (0x08 / 4)
#define R_FLASH_SR             (0x0c / 4)
#define R_FLASH_CR             (0x10 / 4)
#define R_FLASH_AR             (0x14 / 4)
#define R_FLASH_RESERVED       (0x18 / 4)
#define R_FLASH_OBR            (0x1c / 4)
#define R_FLASH_WRPR           (0x20 / 4)
#define R_FLASH_MAX            (0x24 / 4)




///
#define FLASH_CR_PG_Pos                     (0U)                               
#define FLASH_CR_PG_Msk                     (0x1U << FLASH_CR_PG_Pos)          /*!< 0x00000001 */
#define FLASH_CR_PG                         FLASH_CR_PG_Msk                    /*!< Programming */
#define FLASH_CR_PER_Pos                    (1U)                               
#define FLASH_CR_PER_Msk                    (0x1U << FLASH_CR_PER_Pos)         /*!< 0x00000002 */
#define FLASH_CR_PER                        FLASH_CR_PER_Msk                   /*!< Page Erase */
#define FLASH_CR_MER_Pos                    (2U)                               
#define FLASH_CR_MER_Msk                    (0x1U << FLASH_CR_MER_Pos)         /*!< 0x00000004 */
#define FLASH_CR_MER                        FLASH_CR_MER_Msk                   /*!< Mass Erase */
#define FLASH_CR_OPTPG_Pos                  (4U)                               
#define FLASH_CR_OPTPG_Msk                  (0x1U << FLASH_CR_OPTPG_Pos)       /*!< 0x00000010 */
#define FLASH_CR_OPTPG                      FLASH_CR_OPTPG_Msk                 /*!< Option Byte Programming */
#define FLASH_CR_OPTER_Pos                  (5U)                               
#define FLASH_CR_OPTER_Msk                  (0x1U << FLASH_CR_OPTER_Pos)       /*!< 0x00000020 */
#define FLASH_CR_OPTER                      FLASH_CR_OPTER_Msk                 /*!< Option Byte Erase */
#define FLASH_CR_STRT_Pos                   (6U)                               
#define FLASH_CR_STRT_Msk                   (0x1U << FLASH_CR_STRT_Pos)        /*!< 0x00000040 */
#define FLASH_CR_STRT                       FLASH_CR_STRT_Msk                  /*!< Start */
#define FLASH_CR_LOCK_Pos                   (7U)                               
#define FLASH_CR_LOCK_Msk                   (0x1U << FLASH_CR_LOCK_Pos)        /*!< 0x00000080 */
#define FLASH_CR_LOCK                       FLASH_CR_LOCK_Msk                  /*!< Lock */
#define FLASH_CR_OPTWRE_Pos                 (9U)                               
#define FLASH_CR_OPTWRE_Msk                 (0x1U << FLASH_CR_OPTWRE_Pos)      /*!< 0x00000200 */
#define FLASH_CR_OPTWRE                     FLASH_CR_OPTWRE_Msk                /*!< Option Bytes Write Enable */
#define FLASH_CR_ERRIE_Pos                  (10U)                              
#define FLASH_CR_ERRIE_Msk                  (0x1U << FLASH_CR_ERRIE_Pos)       /*!< 0x00000400 */
#define FLASH_CR_ERRIE                      FLASH_CR_ERRIE_Msk                 /*!< Error Interrupt Enable */
#define FLASH_CR_EOPIE_Pos                  (12U)                              
#define FLASH_CR_EOPIE_Msk                  (0x1U << FLASH_CR_EOPIE_Pos)       /*!< 0x00001000 */
#define FLASH_CR_EOPIE                      FLASH_CR_EOPIE_Msk                 /*!< End of operation interrupt enable */



#define FLASH_KEY1                          0x45670123U                     /*!< FPEC Key1 */
#define FLASH_KEY2                          0xCDEF89ABU                     /*!< FPEC Key2 */

#define  FLASH_OPTKEY1                       FLASH_KEY1                    /*!< Option Byte Key1 */
#define  FLASH_OPTKEY2                       FLASH_KEY2                    /*!< Option Byte Key2 */

///

typedef struct Stm32FlashRegs {
	SysBusDevice busdev;
	MemoryRegion iomem;

	uint32_t ACR;
	uint32_t KEYR;
	uint32_t OPTKEYR;
	uint32_t SR;
	uint32_t CR;
	uint32_t AR;
	uint32_t RESERVED;
	uint32_t OBR;
	uint32_t WRPR;
} Stm32FlashRegs;

static uint64_t
stm32_flash_regs_read(void *arg, hwaddr addr, unsigned int size)
{
	Stm32FlashRegs *s = arg;

	if (size != 4) {
		qemu_log_mask(LOG_UNIMP, "stm32 flash only supports 4-byte reads\n");
		return 0;
	}

	addr >>= 2;
	if (addr >= R_FLASH_MAX) {
		qemu_log_mask(LOG_GUEST_ERROR, "invalid read stm32 flash register 0x%x\n",
		  (unsigned int)addr << 2);
		return 0;
	}

	switch(addr) {
	case R_FLASH_ACR:
		return s->ACR;
	case R_FLASH_KEYR:
		return s->KEYR;
	case R_FLASH_OPTKEYR:
		return s->OPTKEYR;
	case R_FLASH_SR:
		return s->SR;
	case R_FLASH_CR:
		return s->CR;
	case R_FLASH_AR:
		return s->AR;
	case R_FLASH_RESERVED:
		return s->RESERVED;
	case R_FLASH_OBR:
		return s->OBR;
	case R_FLASH_WRPR:
		return s->WRPR;
	}
	return 0;
}


static void
stm32_flash_regs_write(void *arg, hwaddr addr, uint64_t data, unsigned int size)
{
	Stm32FlashRegs *s = arg;

	/* XXX Check periph clock enable. */
	if (size != 4) {
		qemu_log_mask(LOG_UNIMP, "stm32 flash only supports 4-byte writes\n");
		return;
	}

	addr >>= 2;
	if (addr >= R_FLASH_MAX) {
		qemu_log_mask(LOG_GUEST_ERROR, "invalid write stm32 flash register 0x%x\n",
		  (unsigned int)addr << 2);
		return;
	}
	switch(addr) {
	case R_FLASH_ACR:
		s->ACR = data;
		break;

	case R_FLASH_KEYR:
		if (s->KEYR == FLASH_OPTKEY1 && data == FLASH_OPTKEY2) {
#ifdef DEBUG_FLASH
			printf("Flash is unlocked!\n");
#endif            
			s->CR &= ~FLASH_CR_LOCK;
			is_flash_locked = 0;
		}
		s->KEYR = data;
		break;

	case R_FLASH_OPTKEYR:
		s->OPTKEYR = data;
		break;

	case R_FLASH_SR:
		s->SR = data;
		break;

	case R_FLASH_CR:
		if (is_flash_locked == 0 && (data & FLASH_CR_LOCK)) {
			if (data & FLASH_CR_PG)
				hw_error("stm32_flash: Attempted to write flash lock while flash program is on!");
#ifdef DEBUG_FLASH            
			printf("Flash is locked!\n");
#endif            
			//s->CR &= ~FLASH_CR_LOCK;
			is_flash_locked = 1;
            memory_region_set_readonly(&flash->iomem, true);

		} else if ( (s->CR & FLASH_CR_PER) && (data & FLASH_CR_STRT) ) { //erase
			if (data & FLASH_CR_PG || (data & FLASH_CR_LOCK))
				hw_error("stm32_flash: Attempted to erase flash block while flash program/flash lock is on!");
#ifdef DEBUG_FLASH
			printf("start erase address 0x%08X \n", s->AR);
#endif			
            if ( (s->AR % 1024) == 0 && (s->AR >= STM32_FLASH_ADDR_START) && (s->AR <= (STM32_FLASH_ADDR_START+flash->size-1024) ) ) { 
                memset(flash->data+(s->AR-STM32_FLASH_ADDR_START) , 0xFF, 1024);
#ifdef DEBUG_FLASH              
              printf("erased\n");
#endif              
			} else {
				printf("ADDRESS: %u\n", s->AR);
				hw_error("stm32_flash: Attempted to erase flash memory page while address is not alligned!");
			}
            
		} else if (data & FLASH_CR_PG) {
			if (data & FLASH_CR_LOCK || data & FLASH_CR_PER)
				hw_error("stm32_flash: Attempted to write flash program while flash lock/flash erase is on!");
			flash_programming_bit = 1;
            memory_region_set_readonly(&flash->iomem, false);

		} else if (data & ~FLASH_CR_PG) {
			flash_programming_bit = 0;
		}

		s->CR = data;
		break;

	case R_FLASH_AR:
		s->AR = data;
		break;

	case R_FLASH_RESERVED:
		s->RESERVED = data;
		break;

	case R_FLASH_OBR:
		s->OBR = data;
		break;

	case R_FLASH_WRPR:
		s->WRPR = data;
		break;

	}

	return;
}

static const MemoryRegionOps stm32_flash_regs_ops = {
	.read = stm32_flash_regs_read,
	.write = stm32_flash_regs_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
	.impl = {
		.min_access_size = 1,
		.max_access_size = 4,
	}
};

static int
stm32_flash_regs_init(SysBusDevice *dev)
{
	Stm32FlashRegs *s = STM32_FLASH_REGS(dev);

	memory_region_init_io(&s->iomem, OBJECT(s), &stm32_flash_regs_ops, s, "flash-regs", 0x400);
	sysbus_init_mmio(dev, &s->iomem);

	return 0;
}

static void
stm32_flash_regs_reset(DeviceState *ds)
{
	Stm32FlashRegs *s = STM32_FLASH_REGS(ds);

	s->ACR = 0;
	s->KEYR = 0;
	s->OPTKEYR = 0;
	s->SR = 0;
	s->CR = 0;
	s->AR = 0;
	s->RESERVED = 0;
	s->OBR = 0;
	s->WRPR = 0;

	is_flash_locked = 1;
}


static void
stm32_flash_regs_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *sc = SYS_BUS_DEVICE_CLASS(klass);
	sc->init = stm32_flash_regs_init;
	dc->reset = stm32_flash_regs_reset;
}

static const TypeInfo
stm32_crc_info = {
	.name          = "stm32-flash-regs",
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(Stm32FlashRegs),
	.class_init    = stm32_flash_regs_class_init,
};

static void
stm32_crc_register_types(void)
{
	type_register_static(&stm32_crc_info);
}

type_init(stm32_crc_register_types);


