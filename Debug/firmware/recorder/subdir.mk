################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../firmware/recorder/comm_link.cpp \
../firmware/recorder/command_handler.cpp \
../firmware/recorder/state_machine.cpp 

OBJS += \
./firmware/recorder/comm_link.o \
./firmware/recorder/command_handler.o \
./firmware/recorder/state_machine.o 

CPP_DEPS += \
./firmware/recorder/comm_link.d \
./firmware/recorder/command_handler.d \
./firmware/recorder/state_machine.d 


# Each subdirectory must supply rules for building sources it contributes
firmware/recorder/%.o firmware/recorder/%.su firmware/recorder/%.cyclo: ../firmware/recorder/%.cpp firmware/recorder/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m4 -std=gnu++17 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../Core/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -I../FATFS/Target -I../FATFS/App -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Middlewares/Third_Party/FatFs/src -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-firmware-2f-recorder

clean-firmware-2f-recorder:
	-$(RM) ./firmware/recorder/comm_link.cyclo ./firmware/recorder/comm_link.d ./firmware/recorder/comm_link.o ./firmware/recorder/comm_link.su ./firmware/recorder/command_handler.cyclo ./firmware/recorder/command_handler.d ./firmware/recorder/command_handler.o ./firmware/recorder/command_handler.su ./firmware/recorder/state_machine.cyclo ./firmware/recorder/state_machine.d ./firmware/recorder/state_machine.o ./firmware/recorder/state_machine.su

.PHONY: clean-firmware-2f-recorder

