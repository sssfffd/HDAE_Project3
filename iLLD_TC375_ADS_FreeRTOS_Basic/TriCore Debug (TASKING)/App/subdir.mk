################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
"../App/App_AEB.c" \
"../App/App_Comm.c" \
"../App/App_DoIP.c" \
"../App/App_Drive.c" \
"../App/App_RCar.c" \
"../App/App_Shared.c" 

COMPILED_SRCS += \
"App/App_AEB.src" \
"App/App_Comm.src" \
"App/App_DoIP.src" \
"App/App_Drive.src" \
"App/App_RCar.src" \
"App/App_Shared.src" 

C_DEPS += \
"./App/App_AEB.d" \
"./App/App_Comm.d" \
"./App/App_DoIP.d" \
"./App/App_Drive.d" \
"./App/App_RCar.d" \
"./App/App_Shared.d" 

OBJS += \
"App/App_AEB.o" \
"App/App_Comm.o" \
"App/App_DoIP.o" \
"App/App_Drive.o" \
"App/App_RCar.o" \
"App/App_Shared.o" 


# Each subdirectory must supply rules for building sources it contributes
"App/App_AEB.src":"../App/App_AEB.c" "App/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc37x "-fC:/Users/USER/Documents/project3/iLLD_TC375_ADS_FreeRTOS_Basic/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc37x -Y0 -N0 -Z0 -o "$@" "$<"
"App/App_AEB.o":"App/App_AEB.src" "App/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"App/App_Comm.src":"../App/App_Comm.c" "App/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc37x "-fC:/Users/USER/Documents/project3/iLLD_TC375_ADS_FreeRTOS_Basic/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc37x -Y0 -N0 -Z0 -o "$@" "$<"
"App/App_Comm.o":"App/App_Comm.src" "App/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"App/App_DoIP.src":"../App/App_DoIP.c" "App/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc37x "-fC:/Users/USER/Documents/project3/iLLD_TC375_ADS_FreeRTOS_Basic/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc37x -Y0 -N0 -Z0 -o "$@" "$<"
"App/App_DoIP.o":"App/App_DoIP.src" "App/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"App/App_Drive.src":"../App/App_Drive.c" "App/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc37x "-fC:/Users/USER/Documents/project3/iLLD_TC375_ADS_FreeRTOS_Basic/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc37x -Y0 -N0 -Z0 -o "$@" "$<"
"App/App_Drive.o":"App/App_Drive.src" "App/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"App/App_RCar.src":"../App/App_RCar.c" "App/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc37x "-fC:/Users/USER/Documents/project3/iLLD_TC375_ADS_FreeRTOS_Basic/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc37x -Y0 -N0 -Z0 -o "$@" "$<"
"App/App_RCar.o":"App/App_RCar.src" "App/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"App/App_Shared.src":"../App/App_Shared.c" "App/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc37x "-fC:/Users/USER/Documents/project3/iLLD_TC375_ADS_FreeRTOS_Basic/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc37x -Y0 -N0 -Z0 -o "$@" "$<"
"App/App_Shared.o":"App/App_Shared.src" "App/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"

clean: clean-App

clean-App:
	-$(RM) ./App/App_AEB.d ./App/App_AEB.o ./App/App_AEB.src ./App/App_Comm.d ./App/App_Comm.o ./App/App_Comm.src ./App/App_DoIP.d ./App/App_DoIP.o ./App/App_DoIP.src ./App/App_Drive.d ./App/App_Drive.o ./App/App_Drive.src ./App/App_RCar.d ./App/App_RCar.o ./App/App_RCar.src ./App/App_Shared.d ./App/App_Shared.o ./App/App_Shared.src

.PHONY: clean-App

