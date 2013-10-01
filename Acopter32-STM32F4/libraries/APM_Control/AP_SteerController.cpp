// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//	Code by Andrew Tridgell
//  Based upon the roll controller by Paul Riseborough and Jon Challinger
//

#include <AP_Math.h>
#include <AP_HAL.h>
#include "AP_SteerController.h"

extern const AP_HAL::HAL& hal;

const AP_Param::GroupInfo AP_SteerController::var_info[] PROGMEM = {
	// @Param: T_CONST
	// @DisplayName: Steering Time Constant
	// @Description: This controls the time constant in seconds from demanded to achieved bank angle. A value of 0.5 is a good default and will work with nearly all models. Advanced users may want to reduce this time to obtain a faster response but there is no point setting a time less than the aircraft can achieve.
	// @Range: 0.4 1.0
	// @Units: seconds
	// @Increment: 0.1
	// @User: Advanced
	AP_GROUPINFO("TCONST",      0, AP_SteerController, _tau,       0.5f),

	// @Param: P
	// @DisplayName: Steering turning gain
	// @Description: The proportional gain for steering. This should be approximately equal to the diameter of the turning circle of the vehicle at low speed and maximum steering angle
	// @Range: 0.1 10.0
	// @Increment: 0.1
	// @User: User
	AP_GROUPINFO("P",      1, AP_SteerController, _K_P,        1.8f),

	// @Param: I
	// @DisplayName: Integrator Gain
	// @Description: This is the gain from the integral of steering angle. Increasing this gain causes the controller to trim out steady offsets due to an out of trim vehicle.
	// @Range: 0 1.0
	// @Increment: 0.05
	// @User: User
	AP_GROUPINFO("I",        3, AP_SteerController, _K_I,        0.2f),

	// @Param: D
	// @DisplayName: Damping Gain
	// @Description: This adjusts the damping of the steering control loop. This gain helps to reduce steering jitter with vibration. It should be increased in 0.01 increments as too high a value can lead to a high frequency steering oscillation that could overstress the vehicle.
	// @Range: 0 0.1
	// @Increment: 0.01
	// @User: User
	AP_GROUPINFO("D",        4, AP_SteerController, _K_D,        0.005f),

	// @Param: IMAX
	// @DisplayName: Integrator limit
	// @Description: This limits the number of degrees of steering in centi-degrees over which the integrator will operate. At the default setting of 1500 centi-degrees, the integrator will be limited to +- 15 degrees of servo travel. The maximum servo deflection is +- 45 centi-degrees, so the default value represents a 1/3rd of the total control throw which is adequate unless the vehicle is severely out of trim.
	// @Range: 0 4500
	// @Increment: 1
	// @User: Advanced
	AP_GROUPINFO("IMAX",     5, AP_SteerController, _imax,        1500),

	AP_GROUPEND
};


/*
  internal rate controller, called by attitude and rate controller
  public functions
*/
int32_t AP_SteerController::get_steering_out(float desired_accel)
{
	uint32_t tnow = hal.scheduler->millis();
	uint32_t dt = tnow - _last_t;
	if (_last_t == 0 || dt > 1000) {
		dt = 0;
	}
	_last_t = tnow;

    float speed = _ahrs.groundspeed();
    if (speed < 1.0e-6) {
        // with no speed all we can do is center the steering
        return 0;
    }

    // this is a linear approximation of the inverse steering
    // equation for a ground vehicle. It returns steering as an angle from -45 to 45
    float scaler = 1.0f / speed;

	// Calculate the steering rate error (deg/sec) and apply gain scaler
    float desired_rate = desired_accel / speed;
	float rate_error = (ToDeg(desired_rate) - ToDeg(_ahrs.get_gyro().z)) * scaler;
	
	// Calculate equivalent gains so that values for K_P and K_I can be taken across from the old PID law
    // No conversion is required for K_D
	float ki_rate = _K_I * _tau * 45.0f;
	float kp_ff = max((_K_P - _K_I * _tau) * _tau  - _K_D , 0) * 45.0f;
	float delta_time    = (float)dt * 0.001f;
	
	// Multiply roll rate error by _ki_rate and integrate
	// Don't integrate if in stabilise mode as the integrator will wind up against the pilots inputs
	if (ki_rate > 0) {
		// only integrate if gain and time step are positive.
		if (dt > 0) {
		    float integrator_delta = rate_error * ki_rate * delta_time * scaler;
			// prevent the integrator from increasing if steering defln demand is above the upper limit
			if (_last_out < -45) {
                integrator_delta = max(integrator_delta , 0);
            } else if (_last_out > 45) {
                // prevent the integrator from decreasing if steering defln demand is below the lower limit
                integrator_delta = min(integrator_delta, 0);
            }
			_integrator += integrator_delta;
		}
	} else {
		_integrator = 0;
	}
	
    // Scale the integration limit
    float intLimScaled = _imax * 0.01f;

    // Constrain the integrator state
    _integrator = constrain_float(_integrator, -intLimScaled, intLimScaled);
	
	// Calculate the demanded control surface deflection
	_last_out = (rate_error * _K_D * 4.0f) + (desired_rate * kp_ff) * scaler + _integrator;
	
	// Convert to centi-degrees and constrain
	return constrain_float(_last_out * 100, -4500, 4500);
}

void AP_SteerController::reset_I()
{
	_integrator = 0;
}

