// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All right reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Radu Serban
// =============================================================================
//
// Demonstration of a steering path-follower PID controller.
//
// The vehicle reference frame has Z up, X towards the front of the vehicle, and
// Y pointing to the left.
//
// =============================================================================

#include "chrono/core/ChFileutils.h"
#include "chrono/core/ChRealtimeStep.h"

#include "chrono_vehicle/ChVehicleModelData.h"
#include "chrono_vehicle/terrain/RigidTerrain.h"
#include "chrono_vehicle/utils/ChVehicleIrrApp.h"
#include "chrono_vehicle/driver/ChIrrGuiDriver.h"

#include "ModelDefs.h"
#include "hmmwv/HMMWV.h"
#include "generic/Generic_PathFollowerDriver.h"

using namespace chrono;
using namespace hmmwv;
using namespace geometry;

// =============================================================================
// Problem parameters

// Type of tire model (RIGID, LUGRE, FIALA)
TireModelType tire_model = RIGID;

// Type of powertrain model (SHAFTS, SIMPLE)
PowertrainModelType powertrain_model = SHAFTS;

// Input file names for the path-follower driver model
std::string steering_controller_file("generic/driver/SteeringController.json");
std::string speed_controller_file("generic/driver/SpeedController.json");
// std::string path_file("paths/curve.txt");
std::string path_file("paths/ISO_double_lane_change.txt");

// JSON file names for vehicle model, tire models, and (simple) powertrain
std::string vehicle_file("generic/vehicle/Vehicle_DoubleWishbones.json");
std::string rigidtire_file("generic/tire/RigidTire.json");
std::string lugretire_file("generic/tire/LugreTire.json");
std::string simplepowertrain_file("generic/powertrain/SimplePowertrain.json");

// Initial vehicle location and orientation
ChVector<> initLoc(-125, -125, 0.5);
ChQuaternion<> initRot(1, 0, 0, 0);

// Desired vehicle speed (m/s)
double target_speed = 12;

// Rigid terrain dimensions
double terrainHeight = 0;
double terrainLength = 300.0;  // size in X direction
double terrainWidth = 300.0;   // size in Y direction

// Point on chassis tracked by the chase camera
ChVector<> trackPoint(0.0, 0.0, 1.75);

// Simulation step size
double step_size = 1e-3;
double tire_step_size = step_size;

// Render FPS
double fps = 30;

// Debug logging
bool debug_output = false;
double debug_fps = 10;

// Output directories
const std::string out_dir = "../STEERING_CONTROLLER";
const std::string pov_dir = out_dir + "/POVRAY";

// POV-Ray output
bool povray_output = true;

// =============================================================================

// Custom Irrlicht event receiver for selecting current driver model.
class ChDriverSelector : public irr::IEventReceiver {
  public:
    ChDriverSelector(const ChVehicle& vehicle, Generic_PathFollowerDriver* driver_follower, ChIrrGuiDriver* driver_gui)
        : m_vehicle(vehicle),
          m_driver_follower(driver_follower),
          m_driver_gui(driver_gui),
          m_driver(m_driver_gui),
          m_using_gui(true) {}

    ChDriver* GetDriver() { return m_driver; }
    bool UsingGUI() const { return m_using_gui; }

    virtual bool OnEvent(const irr::SEvent& event) {
        // Only interpret keyboard inputs.
        if (event.EventType != irr::EET_KEY_INPUT_EVENT)
            return false;

        // Disregard key pressed
        if (event.KeyInput.PressedDown)
            return false;

        switch (event.KeyInput.Key) {
            case irr::KEY_COMMA:
                if (m_using_gui) {
                    m_driver = m_driver_follower;
                    m_using_gui = false;
                }
                return true;
            case irr::KEY_PERIOD:
                if (!m_using_gui) {
                    m_driver_gui->SetThrottle(m_driver_follower->GetThrottle());
                    m_driver_gui->SetSteering(m_driver_follower->GetSteering());
                    m_driver_gui->SetBraking(m_driver_follower->GetBraking());
                    m_driver = m_driver_gui;
                    m_using_gui = true;
                }
                return true;
            case irr::KEY_HOME:
                if (!m_using_gui && !m_driver_follower->GetSteeringController().IsDataCollectionEnabled()) {
                    std::cout << "Data collection started at t = " << m_vehicle.GetChTime() << std::endl;
                    m_driver_follower->GetSteeringController().StartDataCollection();
                }
                return true;
            case irr::KEY_END:
                if (!m_using_gui && m_driver_follower->GetSteeringController().IsDataCollectionEnabled()) {
                    std::cout << "Data collection stopped at t = " << m_vehicle.GetChTime() << std::endl;
                    m_driver_follower->GetSteeringController().StopDataCollection();
                }
                return true;
            case irr::KEY_INSERT:
                if (!m_using_gui && m_driver_follower->GetSteeringController().IsDataAvailable()) {
                    char filename[100];
                    sprintf(filename, "controller_%.2f.out", m_vehicle.GetChTime());
                    std::cout << "Data written to file " << filename << std::endl;
                    m_driver_follower->GetSteeringController().WriteOutputFile(std::string(filename));
                }
                return true;
        }

        return false;
    }

  private:
    bool m_using_gui;
    const ChVehicle& m_vehicle;
    Generic_PathFollowerDriver* m_driver_follower;
    ChIrrGuiDriver* m_driver_gui;
    ChDriver* m_driver;
};

// =============================================================================

int main(int argc, char* argv[]) {
    // ------------------------------
    // Create the vehicle and terrain
    // ------------------------------

    // Create the HMMWV vehicle, set parameters, and initialize
    HMMWV_Full my_hmmwv;
    my_hmmwv.SetChassisFixed(false);
    my_hmmwv.SetChassisVis(PRIMITIVES);
    my_hmmwv.SetWheelVis(PRIMITIVES);
    my_hmmwv.SetInitPosition(ChCoordsys<>(initLoc, initRot));
    my_hmmwv.SetPowertrainType(powertrain_model);
    my_hmmwv.SetDriveType(RWD);
    my_hmmwv.SetTireType(tire_model);
    my_hmmwv.SetTireStepSize(tire_step_size);
    my_hmmwv.Initialize();

    // Create the terrain
    RigidTerrain terrain(my_hmmwv.GetSystem(), terrainHeight, terrainLength, terrainWidth, 0.9,
                         GetChronoDataFile("textures/tile4.jpg"), 200, 200);

    // ----------------------
    // Create the Bezier path
    // ----------------------

    ChBezierCurve* path = ChBezierCurve::read(vehicle::GetDataFile(path_file));
    ////path->write("my_path.txt");

    // ---------------------------------------
    // Create the vehicle Irrlicht application
    // ---------------------------------------

    ChVehicleIrrApp app(my_hmmwv.GetVehicle(), my_hmmwv.GetPowertrain(), L"Steering Controller Demo",
                        irr::core::dimension2d<irr::u32>(800, 640));

    app.SetHUDLocation(500, 20);
    app.SetSkyBox();
    app.AddTypicalLogo();
    app.AddTypicalLights(irr::core::vector3df(-150.f, -150.f, 200.f), irr::core::vector3df(-150.f, 150.f, 200.f), 100,
                         100);
    app.AddTypicalLights(irr::core::vector3df(150.f, -150.f, 200.f), irr::core::vector3df(150.0f, 150.f, 200.f), 100,
                         100);
    app.EnableGrid(false);
    app.SetChaseCamera(trackPoint, 6.0, 0.5);

    app.SetTimestep(step_size);

    // Visualization of controller points (sentinel & target)
    irr::scene::IMeshSceneNode* ballS = app.GetSceneManager()->addSphereSceneNode(0.1f);
    irr::scene::IMeshSceneNode* ballT = app.GetSceneManager()->addSphereSceneNode(0.1f);
    ballS->getMaterial(0).EmissiveColor = irr::video::SColor(0, 255, 0, 0);
    ballT->getMaterial(0).EmissiveColor = irr::video::SColor(0, 0, 255, 0);

    // -------------------------
    // Create the driver systems
    // -------------------------

    // Create both a GUI driver and a path-follower and allow switching between them
    ChIrrGuiDriver driver_gui(app, my_hmmwv.GetVehicle(), my_hmmwv.GetPowertrain());

    /*
    Generic_PathFollowerDriver driver_follower(my_hmmwv.GetVehicle(), path);
    driver_follower.GetSteeringController().SetLookAheadDistance(5);
    driver_follower.GetSteeringController().SetGains(0.5, 0, 0);
    driver_follower.GetSpeedController().SetGains(0.4, 0, 0);
    */
    Generic_PathFollowerDriver driver_follower(my_hmmwv.GetVehicle(), vehicle::GetDataFile(steering_controller_file),
                                               vehicle::GetDataFile(speed_controller_file), path, "my_path");

    driver_follower.SetDesiredSpeed(target_speed);

    // Create and register a custom Irrlicht event receiver to allow selecting the
    // current driver model.
    ChDriverSelector selector(my_hmmwv.GetVehicle(), &driver_follower, &driver_gui);
    app.SetUserEventReceiver(&selector);

    // Finalize construction of visualization assets
    app.AssetBindAll();
    app.AssetUpdateAll();

    // -----------------
    // Initialize output
    // -----------------

    if (povray_output) {
        if (ChFileutils::MakeDirectory(out_dir.c_str()) < 0) {
            std::cout << "Error creating directory " << out_dir << std::endl;
            return 1;
        }
        if (ChFileutils::MakeDirectory(pov_dir.c_str()) < 0) {
            std::cout << "Error creating directory " << pov_dir << std::endl;
            return 1;
        }
        my_hmmwv.ExportMeshPovray(out_dir);
        driver_follower.ExportPathPovray(out_dir);
    }

    // ---------------
    // Simulation loop
    // ---------------

    // Driver location in vehicle local frame
    ChVector<> driver_pos = my_hmmwv.GetVehicle().GetLocalDriverCoordsys().pos;

    // Number of simulation steps between miscellaneous events
    double render_step_size = 1 / fps;
    int render_steps = (int)std::ceil(render_step_size / step_size);
    double debug_step_size = 1 / debug_fps;
    int debug_steps = (int)std::ceil(debug_step_size / step_size);

    // Initialize simulation frame counter and simulation time
    ChRealtimeStepTimer realtime_timer;
    int sim_frame = 0;
    int render_frame = 0;

    while (app.GetDevice()->run()) {
        double time = my_hmmwv.GetSystem()->GetChTime();

        // Update sentinel and target location markers for the path-follower controller.
        // Note that we do this whether or not we are currently using the path-follower driver.
        const ChVector<>& pS = driver_follower.GetSteeringController().GetSentinelLocation();
        const ChVector<>& pT = driver_follower.GetSteeringController().GetTargetLocation();
        ballS->setPosition(irr::core::vector3df((irr::f32)pS.x, (irr::f32)pS.y, (irr::f32)pS.z));
        ballT->setPosition(irr::core::vector3df((irr::f32)pT.x, (irr::f32)pT.y, (irr::f32)pT.z));

        // Render scene and output POV-Ray data
        if (sim_frame % render_steps == 0) {
            app.BeginScene(true, true, irr::video::SColor(255, 140, 161, 192));
            app.DrawAll();
            app.EndScene();

            if (povray_output) {
                char filename[100];
                sprintf(filename, "%s/data_%03d.dat", pov_dir.c_str(), render_frame + 1);
                utils::WriteShapesPovray(my_hmmwv.GetSystem(), filename);
            }

            render_frame++;
        }

        // Debug logging
        if (debug_output && sim_frame % debug_steps == 0) {
            ChVector<> driver_acc = my_hmmwv.GetVehicle().GetVehicleAcceleration(driver_pos);
            GetLog() << "driver acceleration:  " << driver_acc.x << "  " << driver_acc.y << "  " << driver_acc.z
                     << "\n";
            ChVector<> acc = my_hmmwv.GetVehicle().GetChassis()->GetPos_dtdt();
            GetLog() << "CG acceleration:      " << acc.x << "  " << acc.y << "  " << acc.z << "\n";
            GetLog() << "\n";
        }

        // Collect output data from modules (for inter-module communication)
        double throttle_input = selector.GetDriver()->GetThrottle();
        double steering_input = selector.GetDriver()->GetSteering();
        double braking_input = selector.GetDriver()->GetBraking();

        // Update modules (process inputs from other modules)
        driver_follower.Update(time);
        driver_gui.Update(time);
        terrain.Update(time);
        my_hmmwv.Update(time, steering_input, braking_input, throttle_input, terrain);
        std::string msg = selector.UsingGUI() ? "GUI driver" : "Follower driver";
        app.Update(msg, steering_input, throttle_input, braking_input);

        // Advance simulation for one timestep for all modules
        double step = realtime_timer.SuggestSimulationStep(step_size);
        driver_follower.Advance(step);
        driver_gui.Advance(step);
        terrain.Advance(step);
        my_hmmwv.Advance(step);
        app.Advance(step);

        // Increment simulation frame number
        sim_frame++;
    }

    return 0;
}
