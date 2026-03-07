#pragma once

#include <any>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "AExecutor_Strategy.hpp"
#include "../Tools/DeviceControlModule.hpp"

namespace exec {

class EX_DeviceControlModule final : public AExecutorStrategy {
public:
    using Packet = DeviceControlModule::Packet;

private:
    DeviceControlModule* dcm_ = nullptr;
    bool flushAllOnTick_ = false;

public:
    EX_DeviceControlModule() = default;
    ~EX_DeviceControlModule() override = default;

    void init(const Ctx& ctx) override {
        auto it = ctx.find("dcm");
        if (it == ctx.end()) {
            throw std::runtime_error("EX_DeviceControlModule::init(): missing ctx['dcm']");
        }

        dcm_ = std::any_cast<DeviceControlModule*>(it->second);
        if (!dcm_) {
            throw std::runtime_error("EX_DeviceControlModule::init(): ctx['dcm'] is null");
        }

        auto itFlush = ctx.find("flush_all_on_tick");
        if (itFlush != ctx.end()) {
            flushAllOnTick_ = std::any_cast<bool>(itFlush->second);
        }
    }

    std::string name() const override {
        return "EX_DeviceControlModule";
    }

    void execute(const Args& args) override {
        ensureInited();

        if (args.empty()) {
            throw std::runtime_error("EX_DeviceControlModule::execute(): empty args");
        }

        // --------------------------------------------------
        // 1) string keyword / shortcut
        // enqueue("DCM", 10, std::string("showall"));
        // enqueue("DCM", 10, std::string("turnOnAllDigital"));
        // --------------------------------------------------
        if (args.size() == 1 && args[0].type() == typeid(std::string)) {
            const auto& cmd = std::any_cast<const std::string&>(args[0]);

            if (cmd == "turnOnAllDigital") {
                dcm_->enqueueTurnOnAllDigital();
                return;
            }

            if (cmd == "turnOffAllDigital") {
                dcm_->enqueueTurnOffAllDigital();
                return;
            }

            dcm_->enqueueKeyword(cmd);
            return;
        }

        // --------------------------------------------------
        // 2) один Packet
        // enqueue("DCM", 10, DeviceControlModule::Packet{68,0,1});
        // --------------------------------------------------
        if (args.size() == 1 && args[0].type() == typeid(Packet)) {
            dcm_->enqueuePacket(std::any_cast<const Packet&>(args[0]));
            return;
        }

        // --------------------------------------------------
        // 3) vector<Packet>
        // enqueue("DCM", 10, std::vector<Packet>{...});
        // --------------------------------------------------
        if (args.size() == 1 && args[0].type() == typeid(std::vector<Packet>)) {
            dcm_->enqueuePackets(std::any_cast<const std::vector<Packet>&>(args[0]));
            return;
        }

        // --------------------------------------------------
        // 4) tuple<int,int,int>
        // enqueue("DCM", 10, std::tuple<int,int,int>{68,0,1});
        // --------------------------------------------------
        if (args.size() == 1 && args[0].type() == typeid(std::tuple<int,int,int>)) {
            const auto& t = std::any_cast<const std::tuple<int,int,int>&>(args[0]);
            dcm_->enqueuePacket(Packet{
                std::get<0>(t),
                std::get<1>(t),
                std::get<2>(t)
            });
            return;
        }

        // --------------------------------------------------
        // 5) три int подряд
        // enqueue("DCM", 10, 68, 0, 1);
        // --------------------------------------------------
        if (args.size() == 3 &&
            args[0].type() == typeid(int) &&
            args[1].type() == typeid(int) &&
            args[2].type() == typeid(int)) {

            dcm_->enqueuePacket(Packet{
                std::any_cast<int>(args[0]),
                std::any_cast<int>(args[1]),
                std::any_cast<int>(args[2])
            });
            return;
        }

        // --------------------------------------------------
        // 6) setAllState
        // enqueue("DCM", 10,
        //         std::string("setAllState"),
        //         std::array<bool, 8>{...},
        //         std::array<std::uint8_t, 3>{...});
        // --------------------------------------------------
        if (args.size() == 3 &&
            args[0].type() == typeid(std::string) &&
            args[1].type() == typeid(std::array<bool, DeviceControlModule::DIGITAL_COUNT>) &&
            args[2].type() == typeid(std::array<std::uint8_t, DeviceControlModule::PWM_COUNT>)) {

            const auto& cmd = std::any_cast<const std::string&>(args[0]);

            if (cmd != "setAllState") {
                throw std::runtime_error("EX_DeviceControlModule::execute(): unknown 3-arg command");
            }

            const auto& dig =
                std::any_cast<const std::array<bool, DeviceControlModule::DIGITAL_COUNT>&>(args[1]);

            const auto& pwm =
                std::any_cast<const std::array<std::uint8_t, DeviceControlModule::PWM_COUNT>&>(args[2]);

            dcm_->enqueueSetAllHardwareState(dig, pwm);
            return;
        }

        throw std::runtime_error("EX_DeviceControlModule::execute(): unsupported args signature");
    }

    void tick() override {
        ensureInited();

        if (flushAllOnTick_) {
            dcm_->update();
        } else {
            dcm_->tick();
        }
    }

private:
    void ensureInited() const {
        if (!dcm_) {
            throw std::runtime_error("EX_DeviceControlModule: not initialized");
        }
    }
};

} // namespace exec