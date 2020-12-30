// Joanne Baba, Devinn Doering, and Yrina Guarisma 
// Speedometer Program
// Inputs: Photodiode voltage readings, through GPIO pins
// Outputs: Results of speed measurement, displayed to cout and LED lights through GPIO
// Operation: To measure the speeds of people passing through a hallway and comparing them to a user inputted speed limit
//The purpose of this code is to measure the speeds of people passing through a hallway and print to a stats file information about how many
//many people have sped through the hall as well as other general information about the people passing through the hall

#include "gpiolib_addr.h"	//For functions pertaining to the GPIO pins on the Raspberry Pi
#include "gpiolib_reg.h"
#include "gpiolib_reg.c"

#include <stdint.h>
#include <stdio.h>				//for the printf() function
#include <fcntl.h>
#include <linux/watchdog.h> 	//needed for the watchdog specific constants
#include <unistd.h> 			//needed for sleep
#include <sys/ioctl.h> 			//needed for the ioctl function
#include <stdlib.h> 			//for atoi
#include <time.h> 				//for time_t and the time() function
#include <sys/time.h>           //for gettimeofday()

//Below is a macro that had been defined to output appropriate logging messages

//file        - will be the file pointer to the log file
//time        - will be the current time at which the message is being printed
//programName - will be the name of the program, in this case it will be either speedometer to represnt main or measureSpeed if it is in the function
//sev 		  - will be the severity level of the message
//str         - will be a string that contains the message that will be printed to the file.
#define PRINT_MSG(file, time, programName, sev, str) \
	do{ \
			fprintf(logFile, "%s : %s : %s : %s", time, programName, sev, str); \
			fflush(logFile); \
	}while(0)


//Defines a macro to print a value to the log file. 
#define PRINT_VALUE(file, val)	\
	do{	\
		fprintf(logFile, " %f\n", val);	\
		fflush(logFile);	\
	}while(0)


//Defining constants such as pins to be used for inputs and outputs (as well as float max)
#define FLT_MAX 3.402823466e+38F 
#define LASER1_PIN_NUM 4
#define LASER2_PIN_NUM 18 
#define RUNNING_LED_PIN 17
#define WARNING_LED_PIN 22

//Define defaults for values obtained from the config file
#define DEFAULT_SPEED_LIMIT 1
#define DEFAULT_LASER_DISTANCE 3
#define DEFAULT_STATS_FREQUENCY 60

//This is the max amount of time, in seconds, a person is allowed to remain in the hallway before a warning is issued
#define MAX_TIME_IN_HALL 10

//This is the max amount of time, in seconds, a person is allowed to block the laser before a warning is issued
#define LASER_BLOCK_TIME 5

//These define the different levels of severity to easily be accessed by PRINT_MSG later on
#define SEVERITY_DEBUG "severity"
#define SEVERITY_INFO "info"
#define SEVERITY_WARNING "warning"
#define SEVERITY_ERROR "error"
#define SEVERITY_CRITICAL "critical"

//All function declarations
GPIO_Handle initializeGPIO();																																	//Defined on line 240

int laserDiodeStatus(GPIO_Handle gpio, int diodeNumber);																										//Defined on line 256

void setToOutput(GPIO_Handle gpio, int pinNumber);																												//Defined on line 288

void outputOn(GPIO_Handle gpio, int pinNumber);																													//Defined on line 331

void outputOff(GPIO_Handle gpio, int pinNumber);																												//Defined on line 337

void getTime(char* buffer);																																		//Defined on line 342

void readConfig(FILE* configFile, int* timeout, char* logFileName, char* statsFileName, int* statsFrequency, int* speedLimit, int* distanceBetweenLasers);		//Defined on line 363

void computeStats(float* maxSpeed, float* minSpeed, float* averageSpeed, float objectSpeeds[], int peoplePassedThrough);										//Defined on line 490

void measureSpeed(GPIO_Handle gpio, int watchdog, const int statsFrequency, const int speedLimit, const int distance, FILE* logFile, FILE* statsFile);			//Defined on line 511

int main(const int argc, const char* const argv[])	{

	//Create a string that contains the program name
	const char* argName = argv[0];

	//These variables will be used to count how long the name of the program is
	int i = 0;
	int namelength = 0;

	//Find the length of the program name
	while(argName[i] != 0)	{
		namelength++;
		i++;
	} 

	//The name of the program
	char programName[namelength];
	i = 0;

	//Copy the name of the program without the ./ at the start
	//of argv[0]
	while(argName[i + 2] != 0)	{
		programName[i] = argName[i + 2];
		i++;
	} 
	
	//The name of the config file
	const char* configFileName = "/home/pi/speedometer.cfg";

	//Open the config file
	FILE* configFile;
	configFile = fopen(configFileName, "r");

	//Output a warning message to the error stream and the log file if the file cannot be openned
	if(!configFile)	{
		#ifndef RUN_AS_SERVICE
		perror("The config file could not be opened; exiting\n");
		#endif

		return -1;
	}

	//Declare the variables that will be passed to the readConfig function
	int timeout = 0;
	char logFileName[50];
	char statsFileName[50];
	int statsFrequency = DEFAULT_STATS_FREQUENCY;
	int speedLimit = DEFAULT_SPEED_LIMIT;
	int distanceBetweenLasers = DEFAULT_LASER_DISTANCE;

	//Create a char array that will be used to hold the time values
	char time[30];
	getTime(time);

	//Call the readConfig function to read from the config file
	readConfig(configFile, &timeout, logFileName, statsFileName, &statsFrequency, &speedLimit, &distanceBetweenLasers);

	//Close the configFile now that we have finished reading from it
	fclose(configFile);

	//Create a new file pointer to point to the log file and stats file
	FILE* logFile;
	FILE* statsFile;

	//Set it to point to the file from the log and stats files and make it append to
	//the file when it writes to it.
	logFile = fopen(logFileName, "a");
	statsFile = fopen(statsFileName, "a");

	//Check that the file opens properly.
	if(!logFile)	{
		#ifndef RUN_AS_SERVICE
		perror("The log file could not be opened; exiting\n");
		#endif

		return -1;
	}

	#ifndef RUN_AS_SERVICE
	printf("Timeout Time: %d Log File Name: %s statsFileName: %s statsFrequency: %d Speed Limit: %d Distance Between Lasers: %d \n\n", timeout, logFileName, statsFileName, statsFrequency, speedLimit, distanceBetweenLasers);
	#endif

	//Initialize the GPIO pins
	GPIO_Handle gpio = initializeGPIO();
	//Get the current time
	getTime(time);
	//Log that the GPIO pins have been initialized
	PRINT_MSG(logFile, time, programName, SEVERITY_INFO, "The GPIO pins have been initialized\n\n");

	#ifndef RUN_AS_SERVICE
	printf("Successful initialization of the GPIO pins\n");
	#endif

	//This variable will be used to access the /dev/watchdog file, similar to how
	//the GPIO_Handle works
	int watchdog;

	//We use the open function here to open the /dev/watchdog file. If it does
	//not open, then we output an error message. We do not use fopen() because we
	//do not want to create a file if it doesn't exist
	if ((watchdog = open("/dev/watchdog", O_RDWR | O_NOCTTY)) < 0) {
		#ifndef RUN_AS_SERVICE
		printf("Error: Couldn't open watchdog device! %d\n", watchdog);
		#endif

		getTime(time);
		PRINT_MSG(logFile, time, programName, SEVERITY_ERROR, "The watchdog was unable to be opened!\n\n");
		return -1;
	} 
	//Get the current time
	getTime(time);
	//Log that the watchdog file has been opened
	PRINT_MSG(logFile, time, programName, SEVERITY_INFO, "The Watchdog file has been opened\n\n");

	//This line uses the ioctl function to set the time limit of the watchdog
	//timer to 15 seconds. The time limit can not be set higher that 15 seconds
	//so please make a note of that when creating your own programs.
	//If we try to set it to any value greater than 15, then it will reject that
	//value and continue to use the previously set time limit
	ioctl(watchdog, WDIOC_SETTIMEOUT, &timeout);
	
	//Get the current time
	getTime(time);
	//Log that the Watchdog time limit has been set
	PRINT_MSG(logFile, time, programName, SEVERITY_INFO, "The Watchdog time limit has been set\n\n");

	//The value of timeout will be changed to whatever the current time limit of the
	//watchdog timer is
	ioctl(watchdog, WDIOC_GETTIMEOUT, &timeout);

	//This print statement will confirm to us if the time limit has been properly
	//changed.
	#ifndef RUN_AS_SERVICE
	printf("The watchdog timeout is %d seconds.\n\n", timeout);
	#endif

	//Set the pins for the LED's to outputs
	setToOutput(gpio, RUNNING_LED_PIN);
	setToOutput(gpio, WARNING_LED_PIN);

	//Logs that the LED pins have been set to outputs
	getTime(time);
	PRINT_MSG(logFile, time, programName, SEVERITY_INFO, "The LED pins have been set as outputs\n\n");

	//Calls the main function which monitors the hall activity
	measureSpeed(gpio, watchdog, statsFrequency, speedLimit, distanceBetweenLasers, logFile, statsFile);

	return 0;
}

GPIO_Handle initializeGPIO()	{													//This function initializes the GPIO and outputs an error if the GPIO has any issues initializing
	GPIO_Handle gpio;
	gpio = gpiolib_init_gpio();
	if(gpio == NULL)	{
		#ifndef RUN_AS_SERVICE
		perror("Could not initialize GPIO");
		#endif
	}

	return gpio;	
}

//This function should accept the diode number (1 or 2) and output
//a 0 if the laser beam is not reaching the diode, a 1 if the laser
//beam is reaching the diode or -1 if an error occurs.

int laserDiodeStatus(GPIO_Handle gpio, int diodeNumber)	{						//This function checks the status of the given laser diode. Outputs a 1 if the diode is receiving the laser beam and 0 if it is not. 	

	if(gpio == NULL)
		return -1;

	if(diodeNumber == 1)	{
		//If the photodiode is receiving a laser signal, return 1, else, return 0
		uint32_t level_reg = gpiolib_read_reg(gpio, GPLEV(0));

		if(level_reg & (1 << LASER1_PIN_NUM))
			return 1;

		else
			return 0;
	}
	if(diodeNumber == 2)	{

		//If the photodiode is receiving a laser signal, return 1, else, return 0
		uint32_t level_reg = gpiolib_read_reg(gpio, GPLEV(0));

		if(level_reg & (1 << LASER2_PIN_NUM))
			return 1;

		else
			return 0;
	}
	else
		return -1;
}

//This function will change the appropriate pins value in the select register
//so that the pin can function as an output
void setToOutput(GPIO_Handle gpio, int pinNumber)	{
	//Check that the gpio is functional
	if(gpio == NULL)
	{
		#ifndef RUN_AS_SERVICE
		printf("The GPIO has not been intitialized properly \n");
		#endif

		return;
	}

	//Check that we are trying to set a valid pin number
	if(pinNumber < 2 || pinNumber > 27)
	{
		#ifndef RUN_AS_SERVICE
		printf("Not a valid pinNumer \n");
		#endif

		return;
	}

	//This will create a variable that has the appropriate select
	//register number. For more information about the registers
	//look up BCM 2835.
	int registerNum = pinNumber / 10;

	//This will create a variable that is the appropriate amount that 
	//the 1 will need to be shifted by to set the pin to be an output
	int bitShift = (pinNumber % 10) * 3;

	#ifndef RUN_AS_SERVICE
	printf("registerNum: %d\t bitShift: %d\n",registerNum,bitShift);
	#endif

	//This is the same code that was used in Lab 2, except that it uses
	//variables for the register number and the bit shift
	uint32_t sel_reg = gpiolib_read_reg(gpio, GPFSEL(registerNum));
	sel_reg |= 1  << bitShift;
	gpiolib_write_reg(gpio, GPFSEL(registerNum), sel_reg);
}

//This function will make an output pin output 3.3V. It is the same
//as what was done in Lab 2 to make the pin output 3.3V
void outputOn(GPIO_Handle gpio, int pinNumber)	{
	gpiolib_write_reg(gpio, GPSET(0), 1 << pinNumber);
}

//This function will make an output pin turn off. It is the same
//as what was done in Lab 2 to make the pin turn off
void outputOff(GPIO_Handle gpio, int pinNumber)	{
	gpiolib_write_reg(gpio, GPCLR(0), 1 << pinNumber);
}

//This function will get the current time using the gettimeofday function
void getTime(char* buffer)	{
	//Create a timeval struct named tv
  	struct timeval tv;

	//Create a time_t variable named curtime
  	time_t curtime;

	//Get the current time and store it in the tv struct
  	gettimeofday(&tv, NULL); 

	//Set curtime to be equal to the number of seconds in tv
  	curtime=tv.tv_sec;

	//This will set buffer to be equal to a string that in
	//equivalent to the current date, in a month, day, year and
	//the current time in 24 hour notation.
  	strftime(buffer,30,"%m-%d-%Y  %T.",localtime(&curtime));
} 

//This is a function used to read from the config file. It is not implemented very
//well, so when you create your own you should try to create a more effective version
void readConfig(FILE* configFile, int* timeout, char* logFileName, char* statsFileName, int* statsFrequency, int* speedLimit, int* distanceBetweenLasers)	{
	//Loop counter
	int i = 0;
	
	//A char array to act as a buffer for the file
	char buffer[255];

	//The value of the timeout variable is set to zero at the start
	*timeout = 0;

	//The value of duration is set to zero at the start
	*statsFrequency = 0;

	//The value of the numBlinks variable is set to zero at the start
	*speedLimit = 0;

	//The value of distanceBetweenLasers is set to zero at the start
	*distanceBetweenLasers = 0;

	//This is a variable used to track which input we are currently looking
	//for (timeout, logFileName or numBlinks)
	int input = 0;

	//This will 
	//fgets(buffer, 255, configFile);
	//This will check that the file can still be read from and if it can,
	//then the loop will check to see if the line may have any useful 
	//information.
	while(fgets(buffer, 255, configFile) != NULL)	{
		i = 0;
		//If the starting character of the string is a '#', 
		//then we can ignore that line
		if(buffer[i] != '#')	{
			while(buffer[i] != 0)	{
				//This if will check the value of timeout
				if(buffer[i] == '=' && input == 0)	{
					//The loop runs while the character is not null
					while(buffer[i] != 0)	{
						//If the character is a number from 0 to 9
						if(buffer[i] >= '0' && buffer[i] <= '9')	{
							//Move the previous digits up one position and add the
							//new digit
							*timeout = (*timeout * 10) + (buffer[i] - '0');
						}
						i++;
					}
					input++;
				}
				else if(buffer[i] == '=' && input == 1)	{ //This will find the name of the log file
					int j = 0;
					//Loop runs while the character is not a newline or null
					while(buffer[i] != 0  && buffer[i] != '\n')	{
						//If the characters after the equal sign are not spaces or
						//equal signs, then it will add that character to the string
						if(buffer[i] != ' ' && buffer[i] != '=')	{
							logFileName[j] = buffer[i];
							j++;
						}
						i++;
					}
					//Add a null terminator at the end
					logFileName[j] = 0;
					input++;
				}
				else if(buffer[i] == '=' && input == 2)	{ //This will find the name of the log file
					int j = 0;
					//Loop runs while the character is not a newline or null
					while(buffer[i] != 0  && buffer[i] != '\n')	{
						//If the characters after the equal sign are not spaces or
						//equal signs, then it will add that character to the string
						if(buffer[i] != ' ' && buffer[i] != '=')	{
							statsFileName[j] = buffer[i];
							j++;
						}
						i++;
					}
					//Add a null terminator at the end
					statsFileName[j] = 0;
					input++;
				}
				else if(buffer[i] == '=' && input == 3)	{ //This will find the value of duration
					//The loop runs while the character is not null
					while(buffer[i] != 0)	{
						//If the character is a number from 0 to 9
						if(buffer[i] >= '0' && buffer[i] <= '9')	{
							//Move the previous digits up one position and add the
							//new digit
							*statsFrequency = (*statsFrequency * 10) + (buffer[i] - '0');
						}
						i++;
					}
					input++;
				}
				else if(buffer[i] == '=' && input == 4)	{ //This will find the value of speedLimit
					//The loop runs while the character is not null
					while(buffer[i] != 0)	{
						
						//If the character is a number from 0 to 9
						if(buffer[i] >= '0' && buffer[i] <= '9')	{
							//Move the previous digits up one position and add the
							//new digit
							*speedLimit = (*speedLimit * 10) + (buffer[i] - '0');				
						}
						i++;
					}
					input++;
				}
				else if(buffer[i] == '=' && input == 5)	{ //This will find the value of distanceBetweenLasers				
					//The loop runs while the character is not null
					while(buffer[i] != 0)	{
						//If the character is a number from 0 to 9
						if(buffer[i] >= '0' && buffer[i] <= '9')	{
							//Move the previous digits up one position and add the
							//new digit
							*distanceBetweenLasers = (*distanceBetweenLasers * 10) + (buffer[i] - '0');
						}
						i++;
					}
					input++;
				}
				else
					i++;
			}
		}
	}
}

void computeStats(float* maxSpeed, float* minSpeed, float* averageSpeed, float objectSpeeds[], int peoplePassedThrough)	{
	float sumOfSpeeds = 0;
	*maxSpeed = FLT_MAX;
	*minSpeed = 0;
	*averageSpeed = 0;

	for(int i = 0; i < peoplePassedThrough; i++)	{
		if(objectSpeeds[i] > maxSpeed)
			*maxSpeed = objectSpeeds[i];
		if(objectSpeeds[i] < minSpeed)
			*minSpeed = objectSpeeds[i];

		sumOfSpeeds += objectSpeeds[i];
	}

	*averageSpeed = sumOfSpeeds / peoplePassedThrough;

	if(!peoplePassedThrough)
		minSpeed = 0;
}

void measureSpeed(GPIO_Handle gpio, int watchdog, const int statsFrequency, const int speedLimit, const int distance, FILE* logFile, FILE* statsFile)	{
	//Indicates that the program is running, even when puTTy is not connected.
	
	outputOn(gpio, RUNNING_LED_PIN);

	char curTime[30];
	float distanceBetweenLasers = distance / 100.0;

	//If, initially either of the 2 photodiodes are disconnected, wait 1 second then check again. If either or both are still disconnected, exit the program
	while(!laserDiodeStatus(gpio, 1) || !laserDiodeStatus(gpio, 2))	{
		//This ioctl call will write to the watchdog file and prevent the pi from rebooting
		ioctl(watchdog, WDIOC_KEEPALIVE, 0);

		#ifndef RUN_AS_SERVICE
		printf("Distance: %.2f, Laser1: %d, Laser2: %d\n", distanceBetweenLasers, laserDiodeStatus(gpio, 1), laserDiodeStatus(gpio, 2));
		#endif

		sleep(2);
		if(!laserDiodeStatus(gpio, 1) || !laserDiodeStatus(gpio, 2))	{
			getTime(curTime);
			PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_ERROR, "There are no lasers connected to one or more of the photodiodes, exiting.\n\n");

			#ifndef RUN_AS_SERVICE
			perror("No lasers connected!\n");
			#endif
		}
	}

	//Logs that connections with the lasers has been established
	getTime(curTime);

	#ifndef RUN_AS_SERVICE
	printf("Connection with both lasers has been established!\n");
	#endif

	PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_INFO, "Connection with both lasers has been established!");
	
	//If the given speedLimit is less than 0, exit with an error code
	if(speedLimit < 0)	{
		getTime(curTime);

		#ifndef RUN_AS_SERVICE
		perror("Received an invalid speedLimit; exiting\n");
		#endif

		PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_ERROR, "The measureSpeed function was given an invalid speed\n\n");
		return;
	}

	//If the given speedLimit is 0, allow the program to precede but note it in the log file
	if(!speedLimit)	{
		getTime(curTime);

		#ifndef RUN_AS_SERVICE
		perror("Received a speedLimit as 0; running program but flagging all objects walking through hall\n");
		#endif

		PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "The requested speedLimit is 0. Flagging any objects moving through hall\n\n");
	}

	//Enum to establish the different states to be used in the state maching
	enum objectLocation { NOONE_IN_HALL, IN_HALL_MOVE_LEFT, IN_HALL_MOVE_RIGHT, EXITED_HALL, LASER1_BROKEN_GOING_IN, LASER1_BROKEN_GOING_OUT_INCORRECT, LASER1_BROKEN_GOING_OUT_CORRECT, LASER2_BROKEN_GOING_IN, LASER2_BROKEN_GOING_OUT_INCORRECT, LASER2_BROKEN_GOING_OUT_CORRECT };
	enum objectLocation currentLocation = NOONE_IN_HALL;

	//Declaration of variables to be used when calculating time and an array that will store the speeds of the objects
	float objectSpeed = 0;
	float objectSpeeds[1000];
	
	for(int i = 0; i < 1000; i++)
		objectSpeeds[i] = 0;

	int enteringNewState = 0;
	int peoplePassedThrough = 0;
	int numberOfSpeeders = 0;
	int warningIssued = 1;

	time_t startTime = time(NULL);
	time_t enteringTime = time(NULL);
	time_t exitingTime = time(NULL);
	time_t timeInState = time(NULL);

	//The start time of the program
	char sTime[30];
	getTime(sTime);

	//Always runs this, intermittently printing out stats. We acknowledge that a while(1) is not generally accepted but in this case, this illustrates that the program runs continuously.
	while(1)	{

		//If enough time has elapsed since the last time stats were printed
		if((time(NULL) - startTime) > statsFrequency)	{

			//Some short math in order to find the values of the stats that we are going to print out.
			float* maxSpeed;
			float* minSpeed;
			float* averageSpeed;

			computeStats(maxSpeed, minSpeed, averageSpeed, objectSpeeds, peoplePassedThrough);

			getTime(curTime);

			//Used instead of the macro to print a message with variables. Prints all statistics to the log file
			fprintf(statsFile, "STATS FOR THE TIME BETWEEN %s and %s", sTime, curTime);
			fprintf(statsFile, "The number of people that passed through the hall was: %d\n", peoplePassedThrough);
			fprintf(statsFile, "The number of people speeding through the hall was: %d\n", numberOfSpeeders);

			fprintf(statsFile, "The fastest person that went through the hall travelled at a speed of approximately %.2f m/s\n", *maxSpeed);
			fprintf(statsFile, "The slowest person that went through the hall travelled at a speed of approximately %.2f m/s\n", *minSpeed);
			fprintf(statsFile, "The average speed of the people travelling through the hall was %.2f m/s\n\n\n\n", *averageSpeed);
			fflush(statsFile);

			//Used to reset the values in the array of object speeds
			for(int i = 0; i < peoplePassedThrough; i++)
				objectSpeeds[i] = 0;

			//Resets the number of people that passed through as well as resets the start time
			peoplePassedThrough = 0;
			numberOfSpeeders = 0;
			startTime = time(NULL);
			getTime(sTime);
		}
		
		//Variables used to keep track of photodiode status. 1 = receiving a laser, 0 = no laser detected. 
		int laser1Status = laserDiodeStatus(gpio, 1);
		int laser2Status = laserDiodeStatus(gpio, 2);

		//This ioctl call will write to the watchdog file and prevent the pi from rebooting
		ioctl(watchdog, WDIOC_KEEPALIVE, 0);

		//Sleep every iteration of the loop for a miniscule amount of time to reduce processing power
		usleep(1000);
		
		switch(currentLocation)	{

			case NOONE_IN_HALL:
				if(laser1Status && laser2Status)	{
					break;

				}	//If left laser breaks, a person is entering the hall from the left
				else if(!laser1Status)	{
					currentLocation = LASER1_BROKEN_GOING_IN;
					enteringNewState = 1;
					enteringTime = time(NULL);
					timeInState = time(NULL);

				}	//If right laser breaks, a person is entering the hall from the right
				else if(!laser2Status)	{
					currentLocation = LASER2_BROKEN_GOING_IN;
					enteringNewState = 1;
					enteringTime = time(NULL);
					timeInState = time(NULL);
				}

				break;

			//The reason why these 2 states exist to is allow the watchdog to ping at a consistent rate. While these 2 states could be taken out of the program, they would have to be replaced with
			//a while loop in some of the states which is extremely ugly and, annoying because then the watchdog would have to be constantly pinged in these while loops to prevent a timeout. 
			case LASER1_BROKEN_GOING_IN:

				if(laser1Status)	{	
					currentLocation = IN_HALL_MOVE_RIGHT;
					outputOn(gpio, WARNING_LED_PIN);
				}

				if((time(NULL) - timeInState) > LASER_BLOCK_TIME)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("Hey! Watch out! You are blocking the laser!\n");
					#endif 

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "Someone is blocking the laser!\n\n");
					timeInState = time(NULL);
				}

				break;

			case LASER2_BROKEN_GOING_IN:
				
				if(laser2Status)	{
					currentLocation = IN_HALL_MOVE_LEFT;
					outputOn(gpio, WARNING_LED_PIN);
				}

				if((time(NULL) - timeInState) > LASER_BLOCK_TIME)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("Hey! Watch out! You are blocking the laser!\n");
					#endif 

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "Someone is blocking the laser!\n\n");
					timeInState = time(NULL);
				}

				break;

			case IN_HALL_MOVE_LEFT:
				
				//The first time a person enters the state, this basically starts a timer. 
				if(enteringNewState)	{
					timeInState = time(NULL);
					enteringNewState = 0;
				}

				//If someone enters the hall but does not leave
				if((time(NULL) - timeInState) > MAX_TIME_IN_HALL && !warningIssued)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("Hey! Watch out! You are blocking the hallway!\n");
					#endif

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "Someone is blocking the hallway!\n\n");
					warningIssued = 1;
				}
								
				//Person is leaving the hall in the right direction
				if(!laser1Status)	{
					currentLocation = LASER1_BROKEN_GOING_OUT_CORRECT;
					timeInState = time(NULL);
					outputOff(gpio, WARNING_LED_PIN);
				}

				//Person leaves the hall in the wrong direction
				if(!laser2Status)	{
					currentLocation = LASER2_BROKEN_GOING_OUT_INCORRECT;
					enteringTime = 0;
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("This hallway is one way only! Pick a direction to walk, buddy!\n");
					#endif

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_INFO, "A person turned around in the hallway and walked back out!\n\n");
					enteringNewState = 1;
					timeInState = time(NULL);
					outputOff(gpio, WARNING_LED_PIN);
				}

				break;

			case LASER1_BROKEN_GOING_OUT_CORRECT:
				
				if(laser1Status)	{

					int travelTime = time(NULL) - enteringTime;

					if(!travelTime)	
						objectSpeed = -1;
					else	
						objectSpeed = distanceBetweenLasers / travelTime;

					peoplePassedThrough++;
					currentLocation = EXITED_HALL;
				}

				if((time(NULL) - timeInState) > LASER_BLOCK_TIME)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("Hey! Watch out! You are blocking the laser!\n");
					#endif

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "Someone is blocking the laser!\n\n");
					timeInState = time(NULL);
				}

				break;

			case LASER2_BROKEN_GOING_OUT_INCORRECT:

				if(laser2Status)	
					currentLocation = NOONE_IN_HALL;

				if((time(NULL) - timeInState) > LASER_BLOCK_TIME)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("Hey! Watch out! You are blocking the laser!\n");
					#endif

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "Someone is blocking the laser!\n\n");
					timeInState = time(NULL);
				}

				break;

			case IN_HALL_MOVE_RIGHT:
				
				//The first time a person enters the state, this basically starts a timer. 
				if(enteringNewState)	{
					timeInState = time(NULL);
					enteringNewState = 0;
				}

				//If someone enters the hall but does not leave
				if((time(NULL) - timeInState) > MAX_TIME_IN_HALL && !warningIssued)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("Hey! Watch out! You are blocking the hallway!\n");
					#endif

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "Someone is blocking the hallway!\n\n");
					warningIssued = 1;
				}
				
				//Person leaves the hall in the wrong direction
				if(!laser1Status)	{					
					currentLocation = LASER1_BROKEN_GOING_OUT_INCORRECT;
					enteringTime = 0;
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("This hallway is one way only! Pick a direction to walk, buddy!\n");
					#endif

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_INFO, "A person turned around in the hallway and walked back out!\n\n");
					outputOff(gpio, WARNING_LED_PIN);
				}
				
				//Person is leaving the hall in the right direction
				if(!laser2Status)	{
					currentLocation = LASER2_BROKEN_GOING_OUT_CORRECT;
					timeInState = time(NULL);
					outputOff(gpio, WARNING_LED_PIN);
				}

				break;

			case LASER2_BROKEN_GOING_OUT_CORRECT:
				if(laser2Status)	{

					int travelTime = time(NULL) - enteringTime;
					if(!travelTime)
						objectSpeed = -1;
					// printf("time(NULL): %d \t enteringTime: %d \t travelTime: %d\n", time(NULL), enteringTime, travelTime);
					else
						objectSpeed = distanceBetweenLasers / travelTime;
					// printf("ObjectSpeed: %.2f\n", objectSpeed);
					currentLocation = EXITED_HALL;
					peoplePassedThrough++;
				}

				if((time(NULL) - timeInState) > LASER_BLOCK_TIME)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("Hey! Watch out! You are blocking the laser!\n");
					#endif

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "Someone is blocking the laser!\n\n");
					timeInState = time(NULL);
				}

				break;

			case LASER1_BROKEN_GOING_OUT_INCORRECT:

				if(laser1Status)	{
					currentLocation = NOONE_IN_HALL;
				}

				if((time(NULL) - timeInState) > LASER_BLOCK_TIME && !warningIssued)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("Hey! Watch out! You are blocking the laser!\n");
					#endif

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "Someone is blocking the laser!\n\n");
					timeInState = time(NULL);
				}

				break;

			case EXITED_HALL:
				if(objectSpeed < 0)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("Is that even a person? The speed was off the charts!!\n");
					#endif
					
					for(int i = 0; i < 3; i++)	{
						outputOn(gpio, WARNING_LED_PIN);
						usleep(200000);
						outputOff(gpio, WARNING_LED_PIN);
						usleep(200000);
					}

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_INFO, "An extremely fast... thing just went through the hall\n");
				}
				else if(objectSpeed > speedLimit)	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("SLOW DOWN! You are travelling at %.2f m/s over the speed limit!\n", (objectSpeed - speedLimit));
					#endif

					for(int i = 0; i < 3; i++)	{
						outputOn(gpio, WARNING_LED_PIN);
						usleep(200000);
						outputOff(gpio, WARNING_LED_PIN);
						usleep(200000);
					}

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_WARNING, "A person just speed through the hall at a speed of: ");
					PRINT_VALUE(logFile, objectSpeed);
					objectSpeeds[peoplePassedThrough - 1] = objectSpeed;
					numberOfSpeeders++;
				}
				else	{
					getTime(curTime);

					#ifndef RUN_AS_SERVICE
					printf("The speed of the person passing through the hall was: %.2f\n", objectSpeed);
					#endif

					PRINT_MSG(logFile, curTime, "measureSpeed", SEVERITY_INFO, "A person just passed through the hall with a speed of: ");
					PRINT_VALUE(logFile, objectSpeed);
					objectSpeeds[peoplePassedThrough - 1] = objectSpeed;
				}

				objectSpeed = 0;
				currentLocation = NOONE_IN_HALL;
				break;			
		}
	}
}