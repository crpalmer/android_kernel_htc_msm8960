obj-$(CONFIG_MACH_MONARUDO) += board-monarudo.o
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-audio.o
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-gpiomux.o
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-gpu.o
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-keypad.o
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-pmic.o
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-regulator.o
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-storage.o
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-wifi.o

obj-$(CONFIG_MACH_MONARUDO) += display/

ifdef CONFIG_BT_MSM_SLEEP
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-bluesleep.o
endif

ifdef CONFIG_MSM_CAMERA
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-camera.o
endif

ifdef CONFIG_BT
obj-$(CONFIG_MACH_MONARUDO) += board-monarudo-rfkill.o  htc_4335_wl_reg.o
endif
