################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../firmware/system/init.cpp \
../firmware/system/orchestrator.cpp \
../firmware/system/tasks.cpp 

OBJS += \
./firmware/system/init.o \
./firmware/system/orchestrator.o \
./firmware/system/tasks.o 

CPP_DEPS += \
./firmware/system/init.d \
./firmware/system/orchestrator.d \
./firmware/system/tasks.d 


# Each subdirectory must supply rules for building sources it contributes
firmware/system/%.o firmware/system/%.su firmware/system/%.cyclo: ../firmware/system/%.cpp firmware/system/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m4 -std=gnu++17 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../Core/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -I../FATFS/Target -I../FATFS/App -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Middlewares/Third_Party/FatFs/src -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-firmware-2f-system

clean-firmware-2f-system:
	-$(RM) ./firmware/system/init.cyclo ./firmware/system/init.d ./firmware/system/init.o ./firmware/system/init.su ./firmware/system/orchestrator.cyclo ./firmware/system/orchestrator.d ./firmware/system/orchestrator.o ./firmware/system/orchestrator.su ./firmware/system/tasks.cyclo ./firmware/system/tasks.d ./firmware/system/tasks.o ./firmware/system/tasks.su

.PHONY: clean-firmware-2f-system

