#pragma once
#include <optional>
#include <algorithm>
#include <cstdint>
#include "control/Pid.hpp"
#include "world/Lead.hpp"

enum class AccState : std::uint8_t { Off, Standby, Active };

class Acc {
public:
    Acc() : state_(AccState::Off), setSpeed_(0.0f), 
            speedPid_({0.6f, 0.05f, 0.0f}, kMaxDecel, kMaxAccel),
            gapPid_({0.3f, 0.0f, 0.8f}, kMaxDecel, kMaxAccel), hadLead_(false) {}  

    // A key: Off -> Active (set speed = current speed), Active -> Off, Standby -> Active (resume)
    void toggle(float egoSpeed){
        switch (state_) {
            case AccState::Off:
                state_ = AccState::Active;
                setSpeed_ = egoSpeed;
                speedPid_.reset();
                gapPid_.reset();
                break;
            case AccState::Active:
                state_ = AccState::Off;
                break;
            case AccState::Standby:
                state_ = AccState::Active;
                break;
        }
    }

    // Driver braked: Active -> Standby. Other states: no change.
    void cancel(){
        if (state_ == AccState::Active) {
            state_ = AccState::Standby;
        }
    }

    // Every 20 ms while Active: returns the acceleration command in m/s^2.
    float step(float dt, float egoSpeed, const std::optional<Lead>& lead){
        if (state_ != AccState::Active) {
            return 0.0f;
        }
        
        float command = speedPid_.step(setSpeed_ - egoSpeed, dt);

        if (lead) {
            if (!hadLead_) {
                gapPid_.reset();                                    // new lead: forget old D-term memory
            }
            const float desiredGap = kStandstillGap + kTimeGap * egoSpeed;
            command = std::min(command, gapPid_.step(lead->gap - desiredGap, dt));   // "keep the gap"
        }
        hadLead_ = lead.has_value();
        return command;
    }

    AccState state() const { return state_; }
    float setSpeed() const { return setSpeed_; }

private:
    static constexpr float kTimeGap = 0.5f;        // s; game value so the lead stays on screen (real ACC: 0.8-2.5 s)
    static constexpr float kStandstillGap = 2.0f;  // m
    static constexpr float kMaxAccel = 2.0f;       // m/s^2
    static constexpr float kMaxDecel = -3.5f;      // m/s^2, comfort limit; harder braking is AEB's job

    AccState state_;
    float setSpeed_;
    Pid<float> speedPid_;
    Pid<float> gapPid_;
    bool hadLead_;   // to reset the gap PID when a new lead appears
};
