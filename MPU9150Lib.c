////////////////////////////////////////////////////////////////////////////
//
//  This file is part of MPU9150Lib
//
//  Copyright (c) 2013 Pansenti, LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy of 
//  this software and associated documentation files (the "Software"), to deal in 
//  the Software without restriction, including without limitation the rights to use, 
//  copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the 
//  Software, and to permit persons to whom the Software is furnished to do so, 
//  subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all 
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
//  INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
//  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
//  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION 
//  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

//#include <math.h>

#include "MPU9150Lib.h"
#include "inv_mpu.h"
#include "inv_mpu_dmp_motion_driver.h"
#include "MPUQuaternion.h"
#include "simple_uart.h"
#include "RTC1_ms_timer.h"


#define AK89xx_SECONDARY

////////////////////////////////////////////////////////////////////////////
//
//  The functions below are from the InvenSense SDK example code.
//
//  Original copyright notice below:

/*
 $License:
    Copyright (C) 2011-2012 InvenSense Corporation, All Rights Reserved.
    See included License.txt for License information.
 $
 */

/* These next two functions converts the orientation matrix (see
 * gyro_orientation) to a scalar representation for use by the DMP.
 * NOTE: These functions are borrowed from InvenSense's MPL.
 */

static inline unsigned short inv_row_2_scale(const signed char *row)
{
    unsigned short b;

    if (row[0] > 0)
        b = 0;
    else if (row[0] < 0)
        b = 4;
    else if (row[1] > 0)
        b = 1;
    else if (row[1] < 0)
        b = 5;
    else if (row[2] > 0)
        b = 2;
    else if (row[2] < 0)
        b = 6;
    else
        b = 7;      // error
    return b;
}

/* The sensors can be mounted onto the board in any orientation. The mounting
 * matrix seen below tells the MPL how to rotate the raw data from thei
 * driver(s).
 * TODO: The following matrices refer to the configuration on an internal test
 * board at Invensense. If needed, please modify the matrices to match the
 * chip-to-body matrix for your particular set up.
 */

static signed char gyro_orientation[9] = { 1, 0, 0,
                                           0, 1, 0,
                                           0, 0, 1};

static inline unsigned short inv_orientation_matrix_to_scalar(const signed char *mtx)
{
    unsigned short scalar;
    /*

       XYZ  010_001_000 Identity Matrix
       XZY  001_010_000
       YXZ  010_000_001
       YZX  000_010_001
       ZXY  001_000_010
       ZYX  000_001_010
     */
    scalar = inv_row_2_scale(mtx);
    scalar |= inv_row_2_scale(mtx + 3) << 3;
    scalar |= inv_row_2_scale(mtx + 6) << 6;
    return scalar;
}

//
////////////////////////////////////////////////////////////////////////////


void MPU9150Lib()
{
  //  use calibration if available

//  m_useAccelCalibration = true;
//  m_useMagCalibration = true;
  m_useAccelCalibration = false;
  m_useMagCalibration = false;
  m_device = 0;
}

void selectDevice(int device)
{
  m_device = device;
}

void useAccelCal(boolean useCal)
{
  m_useAccelCalibration = useCal;
}

void disableAccelCal()
{
    if (!m_useAccelCalibration)
        return;
    m_useAccelCalibration = false;

    m_accelOffset[0] = 0;
    m_accelOffset[1] = 0;
    m_accelOffset[2] = 0;

    mpu_set_accel_bias(m_accelOffset);
}

void useMagCal(boolean useCal)
{
  m_useMagCalibration = useCal;
}

boolean MPU9150_init(int mpuRate, int magMix, int magRate, int lpf)
{//10,10,10,0
  struct int_param_s int_param;
  int result;
  // not use cal and m_device is 0
  MPU9150Lib();
  mpu_select_device(m_device);
  dmp_select_device(m_device);

  // magnetic rate must be less than or equal to 100Hz
  if (magRate > 100)
    return false;
  if (magRate < 1)
    return false;

  // record mag interval
  m_magInterval = (unsigned long)(1000 / magRate);
  m_lastMagSample = millis();

  // mpu rate must be less than or equal to 1000Hz
  if (mpuRate > 1000)
    return false;
  if (mpuRate < 1)
    return false;

  m_magMix = magMix;
  m_lastDMPYaw = 0;
  m_lastYaw = 0;

  // get calibration data if it's there
#ifdef calibration
	if(calLibRead(m_device, &m_calData))
	{// use calibration data if it's there and wanted
		m_useMagCalibration &= m_calData.magValid == 1;
		m_useAccelCalibration &= m_calData.accelValid == 1;

		//  Process calibration data for runtime
		if(m_useMagCalibration)
		{
			m_magXOffset = (short)(((long)m_calData.magMaxX + (long)m_calData.magMinX) / 2);
			m_magXRange = m_calData.magMaxX - m_magXOffset;
			m_magYOffset = (short)(((long)m_calData.magMaxY + (long)m_calData.magMinY) / 2);
			m_magYRange = m_calData.magMaxY - m_magYOffset;
			m_magZOffset = (short)(((long)m_calData.magMaxZ + (long)m_calData.magMinZ) / 2);
			m_magZRange = m_calData.magMaxZ - m_magZOffset;
		}

		if(m_useAccelCalibration)
		{
			m_accelOffset[0] = -((long)m_calData.accelMaxX + (long)m_calData.accelMinX) / 2;
			m_accelOffset[1] = -((long)m_calData.accelMaxY + (long)m_calData.accelMinY) / 2;
			m_accelOffset[2] = -((long)m_calData.accelMaxZ + (long)m_calData.accelMinZ) / 2;

			mpu_set_accel_bias(m_accelOffset);

			m_accelXRange = m_calData.accelMaxX + (short)m_accelOffset[0];
			m_accelYRange = m_calData.accelMaxY + (short)m_accelOffset[1];
			m_accelZRange = m_calData.accelMaxZ + (short)m_accelOffset[2];
		}
	}

	else
	{
		m_useMagCalibration = false;
		m_useAccelCalibration = false;
	}
	#endif

	m_useMagCalibration = false;
	m_useAccelCalibration = false;

	#ifdef MPULIB_DEBUG
	if (m_useMagCalibration)
	  simple_uart_putstring((const uint8_t *)"Using mag cal");
	if (m_useAccelCalibration)
	  simple_uart_putstring((const uint8_t *)"Using accel cal");
	#endif

	// mpu9150 register address mapping
	mpu_init_structures();
    
	// Not using interrupts so set up this structure to keep the driver happy

	int_param.cb = NULL;
	int_param.pin = 0;
	int_param.lp_exit = 0;
	int_param.active_low = 1;

	//set gyro and compass
	result = mpu_init(&int_param);
	if (result != 0)
	{
		#ifdef MPULIB_DEBUG
		simple_uart_putstring((const uint8_t *)"mpu_init failed with code: ");
		simple_uart_putinteger10(result);
		#endif
		return false;
	}
	mpu_set_sensors(INV_XYZ_GYRO | INV_XYZ_ACCEL | INV_XYZ_COMPASS);  		// enable all of the sensors
	mpu_configure_fifo(INV_XYZ_GYRO | INV_XYZ_ACCEL); 	//  get accel and gyro data in the FIFO also
	mpu_set_sample_rate(mpuRate);			// set the update rate
	mpu_set_compass_sample_rate(magRate);	// set the compass update rate to match
	if (lpf != 0)	mpu_set_lpf(lpf);       // set the low pass filter

	/* To initialize the DMP:
	* 1. Call dmp_load_motion_driver_firmware(). This pushes the DMP image in
	*    inv_mpu_dmp_motion_driver.h into the MPU memory.
	* 2. Push the gyro and accel orientation matrix to the DMP.
	* 3. Register gesture callbacks. Don't worry, these callbacks won't be
	*    executed unless the corresponding feature is enabled.
	* 4. Call dmp_enable_feature(mask) to enable different features.
	* 5. Call dmp_set_interrupt_mode(mode) to select between continuous
	*    and event-only interrupts.
	* 6. Call dmp_set_fifo_rate(freq) to select a DMP output rate.
	* 7. Call any feature-specific control functions.
	*
	* WARNING: Any DMP related functions should only be called when the MPU is
	* awake (call mpu_set_sensors() with a non-zero value).
	*
	* To enable the DMP, just call mpu_set_dmp_state(1). This function can
	* be called repeatedly to enable and disable the DMP at runtime.
	*
	* The following is a short summary of the features supported in the DMP
	* image provided in inv_mpu_dmp_motion_driver.c:
	* DMP_FEATURE_LP_QUAT: Generate a gyro-only quaternion on the DMP at
	* 200Hz. Integrating the gyro data at higher rates reduces numerical
	* errors (compared to integration on the MCU at a lower sampling rate).
	* DMP_FEATURE_TAP: Detect taps along the X, Y, and Z axes.
	* DMP_FEATURE_ORIENT: Notify the application when the device orientation
	* has changed.
	*/
	#ifdef MPULIB_DEBUG
	simple_uart_putstring((const uint8_t *)"Loading firmware\n\r");
	#endif
	if ((result = dmp_load_motion_driver_firmware()) != 0)
	{// try to load the DMP firmware
		#ifdef MPULIB_DEBUG
		simple_uart_putstring((const uint8_t *)"Failed to load dmp firmware: ");
		simple_uart_putinteger10(result);
		#endif
		return false;
	}

	dmp_set_orientation(inv_orientation_matrix_to_scalar(gyro_orientation)); // set up the correct orientation
	dmp_enable_feature(DMP_FEATURE_6X_LP_QUAT | DMP_FEATURE_SEND_RAW_ACCEL | DMP_FEATURE_SEND_RAW_GYRO);	// determine packet_length
	dmp_set_interrupt_mode(DMP_INT_CONTINUOUS);
	dmp_set_fifo_rate(mpuRate);

	if (mpu_set_dmp_state(1) != 0)
	{
		#ifdef MPULIB_DEBUG
		simple_uart_putstring((const uint8_t *)"mpu_set_dmp_state failed");
		#endif
		return false;
	}

	return true;
}

boolean MPU9150_read()
{
//	short intStatus;
	int result;
	short sensors;
	unsigned char more;
	unsigned long timestamp;

	mpu_select_device(m_device);
	dmp_select_device(m_device);
//	mpu_get_int_status(&intStatus);                       // get the current MPU state

	//0x001D(0d29) is successful
//	if ((intStatus & (MPU_INT_STATUS_DMP | MPU_INT_STATUS_DMP_0)) != (MPU_INT_STATUS_DMP | MPU_INT_STATUS_DMP_0))
//		return false;


	//  get the data from the fifo
	if ((result = dmp_read_fifo(m_rawGyro, m_rawAccel, m_rawQuaternion, &timestamp, &sensors, &more)) != 0)
		return false;

	//  got the fifo data so now get the mag data if it's time
	#ifdef AK89xx_SECONDARY
	if ((millis() - m_lastMagSample) >= m_magInterval)
	{
		if ((result = mpu_get_compass_reg(m_rawMag, &timestamp)) != 0)
		{
			#ifdef MPULIB_DEBUG
			simple_uart_putstring((const uint8_t *)"Failed to read compass: ");
			simple_uart_putinteger10(result);
			simple_uart_putstring((const uint8_t *)"\n\r");
			#endif

			return false;
		}

		//	*** Note mag axes are changed here to align with gyros: Y = -X, X = Y
		m_lastMagSample = millis();

		if (m_useMagCalibration)
		{
			m_calMag[VEC3_Y] = -(short)(((long)(m_rawMag[VEC3_X] - m_magXOffset) * (long)SENSOR_RANGE) / (long)m_magXRange);
			m_calMag[VEC3_X] = (short)(((long)(m_rawMag[VEC3_Y] - m_magYOffset) * (long)SENSOR_RANGE) / (long)m_magYRange);
			m_calMag[VEC3_Z] = (short)(((long)(m_rawMag[VEC3_Z] - m_magZOffset) * (long)SENSOR_RANGE) / (long)m_magZRange);
		}

		else
		{
			m_calMag[VEC3_Y] = -m_rawMag[VEC3_X];
			m_calMag[VEC3_X] = m_rawMag[VEC3_Y];
			m_calMag[VEC3_Z] = m_rawMag[VEC3_Z];
		}
	}
	#endif

	// got the raw data - now process
	m_dmpQuaternion[QUAT_W] = (float)m_rawQuaternion[QUAT_W];  // get float version of quaternion
	m_dmpQuaternion[QUAT_X] = (float)m_rawQuaternion[QUAT_X];
	m_dmpQuaternion[QUAT_Y] = (float)m_rawQuaternion[QUAT_Y];
	m_dmpQuaternion[QUAT_Z] = (float)m_rawQuaternion[QUAT_Z];
//	MPUQuaternionNormalize(m_dmpQuaternion);                 // and normalize

//	MPUQuaternionQuaternionToEuler(m_dmpQuaternion, m_dmpEulerPose);

	#ifdef calibration
	// Scale accel data
	if (m_useAccelCalibration) {
	/*        m_calAccel[VEC3_X] = -(short)((((long)m_rawAccel[VEC3_X] + m_accelOffset[0])
			* (long)SENSOR_RANGE) / (long)m_accelXRange);
	m_calAccel[VEC3_Y] = (short)((((long)m_rawAccel[VEC3_Y] + m_accelOffset[1])
			* (long)SENSOR_RANGE) / (long)m_accelYRange);
	m_calAccel[VEC3_Z] = (short)((((long)m_rawAccel[VEC3_Z] + m_accelOffset[2])
			* (long)SENSOR_RANGE) / (long)m_accelZRange);
	*/        if (m_rawAccel[VEC3_X] >= 0)
	m_calAccel[VEC3_X] = -(short)((((long)m_rawAccel[VEC3_X])
			* (long)SENSOR_RANGE) / (long)m_calData.accelMaxX);
	else
	m_calAccel[VEC3_X] = -(short)((((long)m_rawAccel[VEC3_X])
			* (long)SENSOR_RANGE) / -(long)m_calData.accelMinX);

	if (m_rawAccel[VEC3_Y] >= 0)
	m_calAccel[VEC3_Y] = (short)((((long)m_rawAccel[VEC3_Y])
			* (long)SENSOR_RANGE) / (long)m_calData.accelMaxY);
	else
	m_calAccel[VEC3_Y] = (short)((((long)m_rawAccel[VEC3_Y])
			* (long)SENSOR_RANGE) / -(long)m_calData.accelMinY);

	if (m_rawAccel[VEC3_Z] >= 0)
	m_calAccel[VEC3_Z] = (short)((((long)m_rawAccel[VEC3_Z])
			* (long)SENSOR_RANGE) / (long)m_calData.accelMaxZ);
	else
	m_calAccel[VEC3_Z] = (short)((((long)m_rawAccel[VEC3_Z])
			* (long)SENSOR_RANGE) / -(long)m_calData.accelMinZ);

	}
	else
	{
	m_calAccel[VEC3_X] = -m_rawAccel[VEC3_X];
	m_calAccel[VEC3_Y] = m_rawAccel[VEC3_Y];
	m_calAccel[VEC3_Z] = m_rawAccel[VEC3_Z];
	}
	#endif

	m_calAccel[VEC3_X] = -m_rawAccel[VEC3_X];
	m_calAccel[VEC3_Y] = m_rawAccel[VEC3_Y];
	m_calAccel[VEC3_Z] = m_rawAccel[VEC3_Z];

	dataFusion();

	return true;
}

void dataFusion()
{
	float qMag[4];
	float deltaDMPYaw, deltaMagYaw;
	float newMagYaw, newYaw;
	float temp1[4], unFused[4];
	float unFusedConjugate[4];

	// *** NOTE *** pitch direction swapped here

	m_fusedEulerPose[VEC3_X] = m_dmpEulerPose[VEC3_X];
	m_fusedEulerPose[VEC3_Y] = -m_dmpEulerPose[VEC3_Y];
	m_fusedEulerPose[VEC3_Z] = 0;
	MPUQuaternionEulerToQuaternion(m_fusedEulerPose, unFused);    // create a new quaternion

	deltaDMPYaw = -m_dmpEulerPose[VEC3_Z] + m_lastDMPYaw;         // calculate change in yaw from dmp
	m_lastDMPYaw = m_dmpEulerPose[VEC3_Z];                        // update that

	qMag[QUAT_W] = 0;
	qMag[QUAT_X] = m_calMag[VEC3_X];
	qMag[QUAT_Y] = m_calMag[VEC3_Y];
	qMag[QUAT_Z] = m_calMag[VEC3_Z];

	// Tilt compensate mag with the unfused data (i.e. just roll and pitch with yaw 0)

	MPUQuaternionConjugate(unFused, unFusedConjugate);
	MPUQuaternionMultiply(qMag, unFusedConjugate, temp1);
	MPUQuaternionMultiply(unFused, temp1, qMag);

	// Now fuse this with the dmp yaw gyro information

	newMagYaw = -atan2(qMag[QUAT_Y], qMag[QUAT_X]);

	if (newMagYaw != newMagYaw)
	{                                 // check for nAn
		#ifdef MPULIB_DEBUG
		simple_uart_putstring((const uint8_t *)"***nAn\n");
		#endif
		return;                                                     // just ignore in this case
	}

	if (newMagYaw < 0)
		newMagYaw = 2.0f * (float)M_PI + newMagYaw;                 // need 0 <= newMagYaw <= 2*PI

	newYaw = m_lastYaw + deltaDMPYaw;                             // compute new yaw from change

	if (newYaw > (2.0f * (float)M_PI))                            // need 0 <= newYaw <= 2*PI
		newYaw -= 2.0f * (float)M_PI;
	if (newYaw < 0)
		newYaw += 2.0f * (float)M_PI;

	deltaMagYaw = newMagYaw - newYaw;                             // compute difference

	if (deltaMagYaw >= (float)M_PI)
		deltaMagYaw = deltaMagYaw - 2.0f * (float)M_PI;
	if (deltaMagYaw <= -(float)M_PI)
		deltaMagYaw = (2.0f * (float)M_PI + deltaMagYaw);

	if (m_magMix > 0)
	{
	newYaw += deltaMagYaw / m_magMix;                           // apply some of the correction

		if (newYaw > (2.0f * (float)M_PI))				            // need 0 <= newYaw <= 2*PI
			newYaw -= 2.0f * (float)M_PI;
		if (newYaw < 0)
			newYaw += 2.0f * (float)M_PI;
	}

	m_lastYaw = newYaw;

	if (newYaw > (float)M_PI)
		newYaw -= 2.0f * (float)M_PI;

	m_fusedEulerPose[VEC3_Z] = newYaw;                            // fill in output yaw value

	MPUQuaternionEulerToQuaternion(m_fusedEulerPose, m_fusedQuaternion);
}

void printQuaternionfloat(float *quat)
{
	simple_uart_putstring((const uint8_t *)"w: "); simple_uart_putinteger10((int32_t)quat[QUAT_W]);
	simple_uart_putstring((const uint8_t *)" x: "); simple_uart_putinteger10((int32_t)quat[QUAT_X]);
	simple_uart_putstring((const uint8_t *)" y: "); simple_uart_putinteger10((int32_t)quat[QUAT_Y]);
	simple_uart_putstring((const uint8_t *)" z: "); simple_uart_putinteger10((int32_t)quat[QUAT_Z]);
}

void printQuaternionlong(long *quat)
{
	simple_uart_putstring((const uint8_t *)"w: "); simple_uart_putinteger10((int32_t)quat[QUAT_W]);
	simple_uart_putstring((const uint8_t *)" x: "); simple_uart_putinteger10((int32_t)quat[QUAT_X]);
	simple_uart_putstring((const uint8_t *)" y: "); simple_uart_putinteger10((int32_t)quat[QUAT_Y]);
	simple_uart_putstring((const uint8_t *)" z: "); simple_uart_putinteger10((int32_t)quat[QUAT_Z]);
}

void printVectorshort(short *vec)
{
	simple_uart_putstring((const uint8_t *)"x: "); simple_uart_putinteger10((int32_t)vec[VEC3_X]);
	simple_uart_putstring((const uint8_t *)" y: "); simple_uart_putinteger10((int32_t)vec[VEC3_Y]);
	simple_uart_putstring((const uint8_t *)" z: "); simple_uart_putinteger10((int32_t)vec[VEC3_Z]);
}

void printVectorfloat(float *vec)
{
	simple_uart_putstring((const uint8_t *)"x: "); simple_uart_putinteger10((int32_t)vec[VEC3_X]);
	simple_uart_putstring((const uint8_t *)" y: "); simple_uart_putinteger10((int32_t)vec[VEC3_Y]);
	simple_uart_putstring((const uint8_t *)" z: "); simple_uart_putinteger10((int32_t)vec[VEC3_Z]);
}

void printAngles(float *vec)
{
	simple_uart_putstring((const uint8_t *)"x: "); simple_uart_putinteger10((int32_t)(vec[VEC3_X] * RAD_TO_DEGREE));
	simple_uart_putstring((const uint8_t *)" y: "); simple_uart_putinteger10((int32_t)(vec[VEC3_Y] * RAD_TO_DEGREE));
	simple_uart_putstring((const uint8_t *)" z: "); simple_uart_putinteger10((int32_t)(vec[VEC3_Z] * RAD_TO_DEGREE));
}
