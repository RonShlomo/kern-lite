################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../firmware/sensors/buttons.cpp \
../firmware/sensors/dht11.cpp \
../firmware/sensors/radiation_latch.cpp 

OBJS += \
./firmware/sensors/buttons.o \
./firmware/sensors/dht11.o \
./firmware/sensors/radiation_latch.o 

CPP_DEPS += \
./firmware/sensors/buttons.d \
./firmware/sensors/dht11.d \
./firmware/sensors/radiation_latch.d 


# Each subdirectory must supply rules for building sources it contributes
firmware/sensors/%.o firmware/sensors/%.su firmware/sensors/%.cyclo: ../firmware/sensors/%.cpp firmware/sensors/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m4 -std=gnu++17 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../Core/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -I../FATFS/Target -I../FATFS/App -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Middlewares/Third_Party/FatFs/src -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-firmware-2f-sensors

clean-firmware-2f-sensors:
	-$(RM) ./firmware/sensors/buttons.cyclo ./firmware/sensors/buttons.d ./firmware/sensors/buttons.o ./firmware/sensors/buttons.su ./firmware/sensors/dht11.cyclo ./firmware/sensors/dht11.d ./firmware/sensors/dht11.o ./firmware/sensors/dht11.su ./firmware/sensors/radiation_latch.cyclo ./firmware/sensors/radiation_latch.d ./firmware/sensors/radiation_latch.o ./firmware/sensors/radiation_latch.su

.PHONY: clean-firmware-2f-sensors

