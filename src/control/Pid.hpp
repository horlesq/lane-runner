#pragma once
#include <algorithm>

template <typename T>
class Pid {
public:
    struct Gains {
        T kp;
        T ki;
        T kd;
    };

    // Constructor to initialize the PID controller with gains and output limits.
    Pid(Gains gains, T outMin, T outMax) : 
        gains_(gains), outMin_(outMin), outMax_(outMax), 
        integral_(0), prevError_(0), first_(true) {}

    // Compute the PID output given the current error and time step.
    T step(T error, T dt) {
        const T newIntegral = integral_ + error * dt;
        T derivative = T(0);

        // Calculate derivative only if this is not the first step to avoid a large spike.
        if (!first_) {
            derivative = (error - prevError_) / dt;
        } else {
            first_ = false;
        }
        prevError_ = error;

        // PID formula: output = Kp * error + Ki * integral + Kd * derivative
        T output = gains_.kp * error + gains_.ki * newIntegral + gains_.kd * derivative;
       
        // Clamp the output to the specified limits
        const T clamped = std::clamp(output, outMin_, outMax_);

        // Update the integral only if the output is not clamped to avoid windup
        if (clamped == output) {
            integral_ = newIntegral;
        }

        return clamped;
    }

    // Reset the PID controller state.
    void reset() {
        integral_ = 0;
        prevError_ = 0;
        first_ = true;
    }

private:
    Gains gains_;
    T outMin_;
    T outMax_;
    T integral_;
    T prevError_;
    bool first_;
};
