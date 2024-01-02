#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0xc2b78c96, "module_layout" },
	{ 0xa7d5f92e, "ida_destroy" },
	{ 0xb65e5a32, "class_destroy" },
	{ 0x983c8d39, "pci_unregister_driver" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0xf98b608f, "__pci_register_driver" },
	{ 0x2871e975, "__class_create" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0x389a7161, "pci_request_irq" },
	{ 0x5f39c686, "dma_pool_create" },
	{ 0xa0181a43, "dma_alloc_attrs" },
	{ 0xfb4f3e80, "pci_alloc_irq_vectors_affinity" },
	{ 0x93a219c, "ioremap_nocache" },
	{ 0xbc547b75, "pci_dev_get" },
	{ 0xc485366d, "pci_request_regions" },
	{ 0x15d7f502, "dma_set_coherent_mask" },
	{ 0xdda05fe6, "dma_set_mask" },
	{ 0x347f7814, "pci_set_master" },
	{ 0xb16b9ca5, "pci_enable_device_mem" },
	{ 0x17c294af, "device_create" },
	{ 0xc4952f09, "cdev_add" },
	{ 0x2064fa56, "cdev_init" },
	{ 0xe7a02573, "ida_alloc_range" },
	{ 0xca7a3159, "kmem_cache_alloc_trace" },
	{ 0x8b7196ed, "kmalloc_caches" },
	{ 0x362ef408, "_copy_from_user" },
	{ 0x2f7754a8, "dma_pool_free" },
	{ 0x92540fbf, "finish_wait" },
	{ 0x8ddd8aad, "schedule_timeout" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0x678b96ec, "dma_pool_alloc" },
	{ 0xb30f5fd0, "dma_direct_map_page" },
	{ 0x53b954a2, "up_read" },
	{ 0x65715ec7, "get_user_pages" },
	{ 0x668b19a1, "down_read" },
	{ 0x362586da, "current_task" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x2ea2c95c, "__x86_indirect_thunk_rax" },
	{ 0x94442d4, "dma_direct_unmap_page" },
	{ 0x74c9b7b5, "dma_ops" },
	{ 0xa0d2dfd3, "__put_page" },
	{ 0x948ed22b, "__put_devmap_managed_page" },
	{ 0x587f22d7, "devmap_managed_key" },
	{ 0x3eeb2322, "__wake_up" },
	{ 0xffb7c514, "ida_free" },
	{ 0xbd9cb3ec, "device_destroy" },
	{ 0x22b90774, "cdev_del" },
	{ 0x37879d4d, "pci_disable_device" },
	{ 0x37a0cba, "kfree" },
	{ 0xd87af866, "pci_release_regions" },
	{ 0xedc03953, "iounmap" },
	{ 0xd6229881, "pci_free_irq_vectors" },
	{ 0xd73deab7, "dma_free_attrs" },
	{ 0xb5aa7165, "dma_pool_destroy" },
	{ 0x6c4bd59c, "pci_free_irq" },
	{ 0xc5850110, "printk" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x4e58432e, "pv_ops" },
	{ 0x9ec6ca96, "ktime_get_real_ts64" },
	{ 0xdbf17652, "_raw_spin_lock" },
	{ 0xb252f3dd, "_dev_err" },
	{ 0xf9a482f9, "msleep" },
	{ 0x15ba50a6, "jiffies" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("pci:v0000144Dd0000A808sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "417DFAE39655E9DB4E9B48D");
