CXXFLAGS += -Isources/driver/rk_driver

VPATH += sources/driver/rk_driver

OBJ += $(OBJ_DIR)/rk_wifi.o
OBJ += $(OBJ_DIR)/rk_gpio.o
OBJ += $(OBJ_DIR)/rk_sysfs.o
OBJ += $(OBJ_DIR)/rk_watchdog.o
