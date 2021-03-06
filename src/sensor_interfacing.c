/*
 * sensor_interfacing.c
 *
 *  Created on: Sep 28, 2019
 *      Author: Subangkar
 */
#include "rawsensordata.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <Ecore.h>

#include <device/power.h>


#include <activity_recognition.h>

// --------------------------------------- External Functions ---------------------------------------
int uploadAllFiles(const char* dir);
// --------------------------------------------------------------------------------------------------

#define array_size 10000

#define DATA_UPLOAD_START_DELAY 5

#define SENSOR_FREQ 10

float checking_time = 3; //Checking time duration in seconds
float recording_time = 60; //Recording time duration in seconds

// -------------------------- Status Variables Start ----------------------------------------

service_state_t service_state = STOPPED;

sensor_listener_h listener[SENSOR_LAST + 1];

unsigned long long fsize = 0;

Ecore_Timer* running_timer = NULL, *pause_timer = NULL;

time_t last_uploaded_timestamp=0;

//extern appdata_t appdata;
extern activity_type_e current_activity;

extern char activity_names[][2];

// -------------------------- Status Variables End ----------------------------------------

// -------------------------- Timer Functions Start ----------------------------------------

// -------------------------- Timer Functions End ----------------------------------------

// -------------------------- File Utils Functions Start ----------------------------------------
FILE* fp = NULL;
time_t timestamp_start=0;
time_t timestamp_last_neg_hr=0;
time_t timestamp_last_inv_hr=0;
int last_hr=60;
FILE* create_new_data_file() {
	char fpath[256];
	char cmd[256];
	sprintf(cmd, "mkdir -p %s%s/", app_get_data_path(), "current");
	system(cmd);
	timestamp_start=time(NULL);
	timestamp_last_neg_hr=timestamp_last_inv_hr=timestamp_start;
	sprintf(fpath, "%s%s/_%s_%ld.csv", app_get_data_path(), "current", USER_ID, timestamp_start);
#ifdef DEBUG_ON
	dlog_print(DLOG_INFO, LOG_TAG, ">>> new file opened %s...", fpath);
#endif
	return fopen(fpath, "w");
}

void close_current_data_file() {
	if(!fp) return;
	fclose(fp);
	fp = NULL;
	char cmd[256];
	sprintf(cmd, "mv %s%s/* %s", app_get_data_path(), "current", app_get_data_path());
	system(cmd);
	timestamp_start=0;
#ifdef DEBUG_ON
	dlog_print(DLOG_INFO, LOG_TAG, ">>> current file closed...");
#endif
}
// -------------------------- File Utils Functions End ------------------------------------------

Eina_Bool pause_sensors(void *vc);
void stop_sensors();

void update_sensor_current_val(float val, sensor_t type) {

	static int16_t read_sensors = 0;

	if (!fp) {
		fp = create_new_data_file();
	}

	if (type == ALL) {
		// resets value
		for (float* p = &all_sensor_current_vals.hr;
				p <= &all_sensor_current_vals.grav_z; ++p) {
			*p = val;
		}
		read_sensors = 0;
	} else {
		float* p = &all_sensor_current_vals.hr;
		p += (int) type;
		*p = val;
		read_sensors |= 1 << ((int) type);
	}
	fsize = ftell(fp) / 1024;
	// append only when file size < 1GB and all sensors data have been updated
	if (fsize < 1 * 1024 * 1024 && (read_sensors == 0x0FFF)) {
		struct sensor_values vals = all_sensor_current_vals;
		time_t current_time = time(NULL);
		if(vals.hr > VALID_HR) {
			timestamp_last_inv_hr=current_time;
			timestamp_last_neg_hr=current_time;
		}

		if(vals.hr > 0 && vals.hr < VALID_HR && last_hr>VALID_HR) {
			timestamp_last_inv_hr=current_time;
		}
		if(vals.hr < 0 && last_hr>0) timestamp_last_neg_hr=current_time;

		if(vals.hr > 0 && vals.hr < VALID_HR && last_hr<VALID_HR && current_time-timestamp_last_inv_hr>=INVALID_HR_MAX_DURATION) {
#ifdef DEBUG_ON
			dlog_print(DLOG_INFO, LOG_TAG, "stopping early for inv HR after %ld", current_time-timestamp_last_inv_hr);
#endif
			stop_sensors();
		}
		if(vals.hr < 0 && last_hr<0 && last_hr<0 && current_time-timestamp_last_neg_hr>=NEG_HR_MAX_DURATION){
#ifdef DEBUG_ON
			dlog_print(DLOG_INFO, LOG_TAG, "stopping early for neg HR after %ld", current_time-timestamp_last_inv_hr);
#endif
			stop_sensors();
		}

		last_hr=(int)vals.hr;

		fprintf(fp,
				"%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%ld,%s\n",
				(int) vals.hr, (int) vals.ppg, vals.acc_x, vals.acc_y,
				vals.acc_z, vals.gyr_x, vals.gyr_y, vals.gyr_z, vals.pres,
				vals.grav_x, vals.grav_y, vals.grav_z, time(NULL), activity_names[current_activity]);
		read_sensors = 0;
	}
}

void example_sensor_callback(sensor_h sensor, sensor_event_s *event,
		void *user_data) {
	/*
	 If a callback is used to listen for different sensor types,
	 it can check the sensor type
	 */

	sensor_type_e type;
	sensor_get_type(sensor, &type);
	if (type == SENSOR_HRM) {
		update_sensor_current_val(event->values[0], HEART_RATE);
	}
	if (type == SENSOR_HRM_LED_GREEN) {
		update_sensor_current_val(event->values[0], PPG);
	}
	if (type == SENSOR_ACCELEROMETER) {
		update_sensor_current_val(event->values[0], ACCELEROMETER_X);
		update_sensor_current_val(event->values[1], ACCELEROMETER_Y);
		update_sensor_current_val(event->values[2], ACCELEROMETER_Z);
	}
	if (type == SENSOR_GYROSCOPE) {
		update_sensor_current_val(event->values[0], GYROSCOPE_X);
		update_sensor_current_val(event->values[1], GYROSCOPE_Y);
		update_sensor_current_val(event->values[2], GYROSCOPE_Z);
	}
	if (type == SENSOR_PRESSURE) {
		update_sensor_current_val(event->values[0], PRESSURE);
	}
	if (type == SENSOR_GRAVITY) {
		update_sensor_current_val(event->values[0], GRAVITY_X);
		update_sensor_current_val(event->values[1], GRAVITY_Y);
		update_sensor_current_val(event->values[2], GRAVITY_Z);
	}
}

// ---------------------------- Sensor Utility Functions Declarations Start ------------------------------
Eina_Bool end_sensor(sensor_listener_h listener);

void start_sensor(sensor_type_e sensor_type, void *vc);
// ---------------------------- Sensor Utility Functions Declarations End ------------------------------

void sensor_not_supported(const char* sensor_name) {
	//Record an Error if the sensor is not supported, else continue.
	time_t rawtime;
	struct tm * timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);

#ifdef DEBUG_ON
	dlog_print(DLOG_ERROR, LOG_TAG, "%s not supported! Service is useless, exiting...", sensor_name);
#endif
	service_app_exit();
}

Eina_Bool start_sensors(void *vc);
void stop_sensors();
Eina_Bool pause_sensors(void *vc);
Eina_Bool upload_data(void *vc);

// turn on  service_state
Eina_Bool start_sensors(void *vc) {
#ifdef DEBUG_ON
	dlog_print(DLOG_WARN, LOG_TAG, ">>> start_sensors called...");
#endif
	if (service_state == RUNNING){
		if (pause_timer)
			ecore_timer_reset(pause_timer);
		return ECORE_CALLBACK_PASS_ON;
	}

	// reset/postpone if any upload is scheduled
	if (pause_timer) {
		ecore_timer_del(pause_timer);
		pause_timer = NULL;
	}

#ifdef DEBUG_ON
	dlog_print(DLOG_WARN, LOG_TAG, ">>> starting sensors...");
#endif
	//PPG
	bool supported_PPG = false;
	sensor_type_e sensor_type_PPG = SENSOR_HRM_LED_GREEN;
	sensor_is_supported(sensor_type_PPG, &supported_PPG);
	if (!supported_PPG) {
		sensor_not_supported("PPG");
	} else {
		start_sensor(sensor_type_PPG, vc);
	}

	//HRM
	bool supported_HRM = false;
	sensor_type_e sensor_type_HRM = SENSOR_HRM;
	sensor_is_supported(sensor_type_HRM, &supported_HRM);
	if (!supported_HRM) {
		sensor_not_supported("HRM");
	} else {
		start_sensor(sensor_type_HRM, vc);
	}

	//ACC (x,y,z)
	bool supported_ACC = false;
	sensor_type_e sensor_type_ACC = SENSOR_ACCELEROMETER;
	sensor_is_supported(sensor_type_ACC, &supported_ACC);
	if (!supported_ACC) {
		sensor_not_supported("ACC");
	} else {
		start_sensor(sensor_type_ACC, vc);
	}
	//Gravity (x,y,z)
	bool supported_Gravity = false;
	sensor_type_e sensor_type_Gravity = SENSOR_GRAVITY;
	sensor_is_supported(sensor_type_Gravity, &supported_Gravity);
	if (!supported_Gravity) {
		sensor_not_supported("Gravity");
	} else {
		start_sensor(sensor_type_Gravity, vc);
	}

	//Gyroscope (x,y,z)
	bool supported_Gyro = false;
	sensor_type_e sensor_type_Gyro = SENSOR_GYROSCOPE;
	sensor_is_supported(sensor_type_Gyro, &supported_Gyro);
	if (!supported_Gyro) {
		sensor_not_supported("Gyro");
	} else {
		start_sensor(sensor_type_Gyro, vc);
	}

	//Atmospheric pressure
	bool supported_Pres = false;
	sensor_type_e sensor_type_Pres = SENSOR_PRESSURE;
	sensor_is_supported(sensor_type_Pres, &supported_Pres);
	if (!supported_Pres) {
		sensor_not_supported("Pressure");
	} else {
		start_sensor(sensor_type_Pres, vc);
	}
	service_state = RUNNING;
#ifdef DEBUG_ON
	dlog_print(DLOG_WARN, LOG_TAG, ">>> pause_sensors will be called after %d...", DATA_RECORDING_DURATION);
#endif
	if (!pause_timer)
		pause_timer = ecore_timer_add(DATA_RECORDING_DURATION, pause_sensors, vc);
#ifdef DEBUG_ON
	dlog_print(DLOG_WARN, LOG_TAG, ">>> start_sensors will be called after %d...", DATA_RECORDING_INTERVAL);
#endif
	return ECORE_CALLBACK_RENEW;// renews running_timer
}

// turn off service_state
void stop_sensors() {
	if (service_state == STOPPED)
		return;
#ifdef DEBUG_ON
	dlog_print(DLOG_WARN, LOG_TAG, ">>> stopping sensors...");
#endif
	for (int i = 0; i <= SENSOR_LAST; i++) {
		end_sensor(listener[i]);
	}
	service_state = STOPPED;
}

Eina_Bool pause_sensors(void *vc) {
#ifdef DEBUG_ON
	dlog_print(DLOG_WARN, LOG_TAG, ">>> pause_sensors called...");
#endif
	stop_sensors();
	close_current_data_file();

	upload_data(vc);

	pause_timer=NULL;
	return ECORE_CALLBACK_CANCEL;
}

Eina_Bool upload_data(void *vc){
#ifdef DEBUG_ON
	dlog_print(DLOG_WARN, LOG_TAG, ">>> upload_data called...");
#endif
	// a significant delay is introduced here if no internet connection available
	if(time(NULL)-last_uploaded_timestamp>=WAIT_TIME_UPLOAD && uploadAllFiles(app_get_data_path()))	{
		last_uploaded_timestamp=time(NULL);
	}
#ifdef DEBUG_ON
	else
		dlog_print(DLOG_WARN, LOG_TAG, ">>> skipping upload...");
#endif
	return ECORE_CALLBACK_CANCEL;
}



void start_timed_sensors(void *data) {
#ifdef DEBUG_ON
	dlog_print(DLOG_WARN, LOG_TAG, ">>> start_timed_sensors called...");
#endif
	start_sensors(data);
	if (!running_timer)
		running_timer = ecore_timer_add(DATA_RECORDING_INTERVAL, start_sensors,	data);
	else {
		ecore_timer_reset(running_timer);
	}
}

// ---------------------------- Sensor Utility Functions Definitions Start ------------------------------
// stops single sensor
Eina_Bool end_sensor(sensor_listener_h listener) {
	// Release all resources.
	sensor_listener_stop(listener);
	sensor_destroy_listener(listener);
	return ECORE_CALLBACK_CANCEL;
}

// starts single sensor
void start_sensor(sensor_type_e sensor_type, void *vc) {
	//Set sensors and start recording
	sensor_h sensor;
	sensor_get_default_sensor(sensor_type, &sensor);
	sensor_create_listener(sensor, &listener[sensor_type]);
	sensor_listener_set_interval(listener[sensor_type], 1000 / SENSOR_FREQ);
	sensor_listener_set_event_cb(listener[sensor_type], 1000 / SENSOR_FREQ,	example_sensor_callback, vc); //10Hz
	sensor_listener_set_option(listener[sensor_type], SENSOR_OPTION_ALWAYS_ON);
//	sensor_listener_set_attribute_int(listener[sensor_type], SENSOR_ATTRIBUTE_PAUSE_POLICY, SENSOR_PAUSE_NONE);
	sensor_listener_start(listener[sensor_type]);
}
// ---------------------------- Sensor Utility Functions Definitions End ------------------------------

