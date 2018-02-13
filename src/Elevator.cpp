/*
 * Elevator.cpp
 *
 *  Created on: Jan 12, 2018
 *      Author: DriversStation
 */

#include <Elevator.h>
//#include "ctre/Phoenix.h"
#include <WPILib.h>

//TODO: have limits be hall effects during initialization, but position soft limits after that

#define PI 3.14159265

const int INIT_STATE_E = 0;
const int DOWN_STATE_E = 1;
const int MID_STATE_E = 2;
const int UP_STATE_E = 3;
const int STOP_STATE_E = 4;

const double free_speed_e = 18730.0; //rad/s
const double G_e = (20.0 / 1.0); //gear ratio

const double TICKS_PER_ROT_E = 4096.0; //possibly not
const double PULLEY_DIAMETER = 0.0381; //radius of the pulley in meters

const double MAX_VOLTAGE_E = 12.0; //CANNOT EXCEED abs(12)  //4.0
const double MIN_VOLTAGE_E = -10.0;

//For motion profiler
const double MAX_VELOCITY_E = 1.3; //0.8 //1,3
const double MAX_ACCELERATION_E = 10.0; //lower stutter 5.0
const double TIME_STEP_E = 0.01;

const double friction_loss = 0.78;

const double MAX_THEORETICAL_VELOCITY_E = (free_speed_e / G_e) / 60.0
		* PULLEY_DIAMETER * PI * friction_loss; //m/s
const double Kv_e = 1 / MAX_THEORETICAL_VELOCITY_E;

const int ELEVATOR_SLEEP_TIME = 0;
const double ELEVATOR_WAIT_TIME = 0.01; //sec

int last_elevator_state = 0; //init state

const double DOWN_POS_E = 0.1; //starting pos
const double MID_POS_E = 0.25;
const double UP_POS_E = 0.7;

double offset = 0.0;
double ff = 0.0; //feedforward
double u_e = 0.0; //this is the input in volts to the motor
double v_bat_e = 0.0; //this will be the voltage of the battery at every loop

std::vector<std::vector<double> > K_e;
std::vector<std::vector<double> > K_down_e = { { 13.16, .71 }, { 13.16, .71 } }; //controller matrix that is calculated in the Python simulation
std::vector<std::vector<double> > K_up_e = { { 9.91, 0.51 }, { 9.91, 0.51 } }; //controller matrix that is calculated in the Python simulation

std::vector<std::vector<double> > X_e = { { 0.0 }, //state matrix filled with the state of the states of the system //not used
		{ 0.0 } };

std::vector<std::vector<double> > error_e = { { 0.0 }, { 0.0 } };

ElevatorMotionProfiler *elevator_profiler;

Timer *elevatorTimer = new Timer();

bool is_at_bottom_e = false;
bool is_at_top = false;
bool first_at_bottom_e = false;
bool last_at_bottom_e = false;
bool encs_zeroed_e = false;
bool voltage_safety_e = false;

int init_counter = 0;
int encoder_counter_e = 0;

Elevator::Elevator(PowerDistributionPanel *pdp,
		ElevatorMotionProfiler *elevator_profiler_) {

	hallEffectTop = new DigitalInput(2);
	hallEffectBottom = new DigitalInput(1);

	elevator_profiler = elevator_profiler_;

	elevator_profiler->SetMaxAccElevator(MAX_ACCELERATION_E);
	elevator_profiler->SetMaxVelElevator(MAX_VELOCITY_E);

	talonElevator1 = new TalonSRX(33);
//	talonElevator1->ConfigVoltageCompSaturation(12.0, 0);
//	talonElevator1->EnableVoltageCompensation(true);
	talonElevator1->ConfigSelectedFeedbackSensor(QuadEncoder, 0, 0);

	talonElevator2 = new TalonSRX(0);
//	talonElevator2->ConfigVoltageCompSaturation(12.0, 0);
//	talonElevator2->EnableVoltageCompensation(true);
	talonElevator2->Set(ControlMode::Follower, 33); //re-slaved

	talonElevator1->ConfigContinuousCurrentLimit(40, 0);
	talonElevator2->ConfigContinuousCurrentLimit(40, 0);
	talonElevator1->ConfigPeakCurrentLimit(0, 0);
	talonElevator2->ConfigPeakCurrentLimit(0, 0);
	//talonElevator1->EnableCurrentLimit(true);
	//talonElevator2->EnableCurrentLimit(true);

	talonElevator1->ConfigVelocityMeasurementPeriod(
			VelocityMeasPeriod::Period_10Ms, 0);
	talonElevator1->ConfigVelocityMeasurementWindow(5, 0);

	pdp_e = pdp;

}

void Elevator::InitializeElevator() {

	if (!is_elevator_init) { //don't see hall effect
		//is_elevator_init = false;
		SetVoltageElevator(2.0); //double elevator_volt = (2.0 / pdp_e->GetVoltage()) * -1.0; //up  not called
//		talonElevator1->Set(ControlMode::PercentOutput,
//				elevator_volt);
//		talonElevator2->Set(ControlMode::PercentOutput,
//				elevator_volt);
	}

	//double up_volt = (0.2 * -1.0) / pdp_e->GetVoltage(); //to not crash down
	//talonElevator1->Set(ControlMode::PercentOutput, up_volt);
	//talonElevator2->Set(ControlMode::PercentOutput, up_volt);

}

void Elevator::StopElevator() {

	talonElevator1->Set(ControlMode::PercentOutput, 0.0);
	talonElevator2->Set(ControlMode::PercentOutput, 0.0);

}

void Elevator::Move(std::vector<std::vector<double> > ref_elevator) {

	double current_pos_e = GetElevatorPosition();
	double current_vel_e = GetElevatorVelocity();

	SmartDashboard::PutNumber("ELEV POS", current_pos_e);
	SmartDashboard::PutNumber("ELEV VEL", current_vel_e);

	double goal_pos_e = ref_elevator[0][0];
	double goal_vel_e = ref_elevator[1][0];

	SmartDashboard::PutNumber("ELEV GOAL POS", goal_pos_e); //goal is the individual points from the profiler
	SmartDashboard::PutNumber("ELEV GOAL VEL", goal_vel_e);

	error_e[0][0] = goal_pos_e - current_pos_e;
	error_e[1][0] = goal_vel_e - current_vel_e;

	SmartDashboard::PutNumber("ELEV ERR POS", error_e[0][0]);
	SmartDashboard::PutNumber("ELEV ERR VEL", error_e[1][0]);

	v_bat_e = 12.0;

	if (elevator_profiler->final_goal_e < current_pos_e) { //can't be the next goal in case we get ahead of the profiler
		K_e = K_down_e;
		ff = 0.0;
		offset = 1.0; //will go less down, nothing is reversed until end of setvoltage()
		SmartDashboard::PutString("ELEV GAINS", "DOWN");
	} else {
		offset = 0.0;
		K_e = K_up_e;
		ff = (Kv_e * goal_vel_e * v_bat_e);
		SmartDashboard::PutString("ELEV GAINS", "UP");
	}

	u_e = (K_e[0][0] * error_e[0][0]) + (K_e[0][1] * error_e[1][0]);

//	std::cout << "FF:  " << ff << "  FB: " << u_e << std::endl;

	u_e += ff + offset;

	SmartDashboard::PutNumber("ELEV CONT VOLTAGE", u_e);

	SetVoltageElevator(u_e);

}

void Elevator::SetVoltageElevator(double elevator_voltage) {

//	int enc = talonElevator1->GetSensorCollection().GetQuadraturePosition(); //encoders return ints?
//	SmartDashboard::PutNumber("ElEV ENC", enc);

	SmartDashboard::PutNumber("ELEV POS", GetElevatorPosition());
	SmartDashboard::PutNumber("ELEV VEL", GetElevatorVelocity());

	is_at_bottom_e = IsAtBottomElevator();
	is_at_top = IsAtTopElevator();

	SmartDashboard::PutNumber("ELEV HALL EFF BOT", is_at_bottom_e);
	SmartDashboard::PutNumber("ELEV HALL EFF TOP", is_at_top);

	SmartDashboard::PutString("ELEV safety", "none");

	//upper soft limit
	if (GetElevatorPosition() >= (0.9) && elevator_voltage > 0.0) { //at max height and still trying to move up
		elevator_voltage = 0.0;
		SmartDashboard::PutString("ELEV safety", "soft limit");
	}
//
//	//lower soft limit
	if (GetElevatorPosition() <= (-0.1) && elevator_voltage < 0.0) { //at max height and still trying to move up
		elevator_voltage = 0.0;
		SmartDashboard::PutString("ELEV safety", "soft limit");
	}

//	//zero last time seen, on way up //does not zero voltage
	if (!is_at_bottom_e) {
		if (last_at_bottom_e) {
			ZeroEncs();
			last_at_bottom_e = false;
		}
	} else {
		last_at_bottom_e = true;
	}

	//if (elevator_voltage < 0.0) { //account for gravity
	//	elevator_voltage += 1.5;
	//}

	//clip
	if (elevator_voltage > MAX_VOLTAGE_E) {
		elevator_voltage = MAX_VOLTAGE_E;
		SmartDashboard::PutString("ELEV safety", "clipping");
	} else if (elevator_voltage < MIN_VOLTAGE_E) {
		elevator_voltage = MIN_VOLTAGE_E;
		SmartDashboard::PutString("ELEV safety", "clipping");
	}

	if (std::abs(GetElevatorVelocity()) <= 0.1 && std::abs(elevator_voltage) > 2.0) { //this has to be here at the end
		encoder_counter_e++;
	} else {
		encoder_counter_e = 0;
	}
	if (encoder_counter_e > 10) { //10
		elevator_voltage = 0.0;
		SmartDashboard::PutString("ELEV safety", "stall");
	}

	SmartDashboard::PutNumber("ELEV VOLTAGE", elevator_voltage);

	elevator_voltage /= 12.0;

	elevator_voltage *= -1.0; //reverse at END

	//std::cout << "EL OUTPUT " << elevator_voltage << std::endl;

	//2 is not slaved to 1
	talonElevator1->Set(ControlMode::PercentOutput, elevator_voltage);
	//talonElevator2->Set(ControlMode::PercentOutput, elevator_voltage);

}

bool Elevator::ElevatorEncodersRunning() {

//	double current_pos_e = GetElevatorPosition();
//	double current_ref_e = elevator_profiler->GetNextRefElevator().at(0).at(0);

//	if (talonElevator1->GetOutputCurrent() > 5.0
//			&& std::abs(talonElevator1->GetSelectedSensorVelocity(0)) <= 0.2
//			&& std::abs(current_ref_e - current_pos_e) > 0.2) {
//		return false;
//	}


	return true;
}

double Elevator::GetElevatorPosition() {

	//divide by the native ticks per rotation then multiply by the circumference of the pulley
	//radians
	double elevator_pos =
			(talonElevator1->GetSensorCollection().GetQuadraturePosition()
					/ TICKS_PER_ROT_E) * (PI * PULLEY_DIAMETER) * -1.0;
	return elevator_pos;

}

double Elevator::GetElevatorVelocity() {

	//native units are ticks per 100 ms so we multiply the whole thing by 10 to get it into per second. Then divide by ticks per rotation to get into
	//RPS then muliply by circumference for m/s
	double elevator_vel =
			(talonElevator1->GetSensorCollection().GetQuadratureVelocity()
					/ (TICKS_PER_ROT_E)) * (PULLEY_DIAMETER * PI) * (10.0)
					* -1.0;
	return elevator_vel;

}

bool Elevator::IsAtBottomElevator() {
	if(!hallEffectBottom->Get()) { //has to be messy form
		return true;
	}
	else {
		return false;
	}
}

bool Elevator::IsAtTopElevator() {
	if(!hallEffectTop->Get()) {
		return true;
	}
	else {
		return false;
	}
}

void Elevator::ManualElevator(Joystick *joyOpElev) {

	//SmartDashboard::PutNumber("ELEV CUR", talonElevator1->GetOutputCurrent());
	//SmartDashboard::PutNumber("ElEV ENC",
	//	talonElevator1->GetSelectedSensorPosition(0));

	//SmartDashboard::PutNumber("ELEV HEIGHT", GetElevatorPosition());

//	double output = (joyOpElev->GetY()) * 0.5 * 12.0; //multiply by voltage because setvoltageelevator takes voltage
//
//	SetVoltageElevator(output);

}

void Elevator::ElevatorStateMachine() {

	switch (elevator_state) {

	case INIT_STATE_E:
		SmartDashboard::PutString("ELEVATOR", "INIT");
		InitializeElevator();
		if (is_elevator_init) {
			elevator_state = DOWN_STATE_E;
		}
		last_elevator_state = INIT_STATE_E;
		break;

	case DOWN_STATE_E:
		SmartDashboard::PutString("ELEVATOR", "DOWN");
		if (last_elevator_state != DOWN_STATE_E) { //first time in state
			elevator_profiler->SetMaxAccElevator(MAX_ACCELERATION_E); //TRY TAKING THESE OUT
			elevator_profiler->SetMaxVelElevator(MAX_VELOCITY_E);
			elevator_profiler->SetFinalGoalElevator(DOWN_POS_E);
			elevator_profiler->SetInitPosElevator(GetElevatorPosition());
		}
		last_elevator_state = DOWN_STATE_E;
		break;

	case MID_STATE_E:
		SmartDashboard::PutString("ELEVATOR", "MID");
		if (last_elevator_state != MID_STATE_E) { //first time in state
			elevator_profiler->SetMaxAccElevator(MAX_ACCELERATION_E);
			elevator_profiler->SetMaxVelElevator(MAX_VELOCITY_E);
			elevator_profiler->SetFinalGoalElevator(MID_POS_E);
			elevator_profiler->SetInitPosElevator(GetElevatorPosition());
		}
		last_elevator_state = MID_STATE_E;
		break;

	case UP_STATE_E:
		SmartDashboard::PutString("ELEVATOR", "UP");
		if (last_elevator_state != UP_STATE_E) { //first time in state
			elevator_profiler->SetMaxAccElevator(MAX_ACCELERATION_E);
			elevator_profiler->SetMaxVelElevator(MAX_VELOCITY_E);
			elevator_profiler->SetFinalGoalElevator(UP_POS_E);
			elevator_profiler->SetInitPosElevator(GetElevatorPosition());
		}
		last_elevator_state = UP_STATE_E;
		break;

	case STOP_STATE_E:
		SmartDashboard::PutString("ELEVATOR", "STOP");
		StopElevator();
		last_elevator_state = STOP_STATE_E;
		break;

	}

}

void Elevator::StartElevatorThread() {

	Elevator *elevator_ = this;

	ElevatorThread = std::thread(&Elevator::ElevatorWrapper, elevator_);
	ElevatorThread.detach();

}

void Elevator::ElevatorWrapper(Elevator *el) {

	elevatorTimer->Start();

	while (true) {
		while (frc::RobotState::IsEnabled()) {
			std::this_thread::sleep_for(
					std::chrono::milliseconds(ELEVATOR_SLEEP_TIME));

			if (elevatorTimer->HasPeriodPassed(ELEVATOR_WAIT_TIME)) {

				//std::cout << "pos: " << profile_elevator.at(0).at(0) << "    " // std::endl; //"  "
				//		<< "vel: " << profile_elevator.at(1).at(0) << "   "
				//		<< "acc: " << profile_elevator.at(2).at(0) << "   " << std::endl;
				//	std::cout << "ref: " << profile_elevator.at(3).at(0)
				//		<< std::endl; //ref is 0 //and current_pos is 0// still

				if (el->elevator_state != STOP_STATE_E
						&& el->elevator_state != INIT_STATE_E) {

					std::vector<std::vector<double>> profile_elevator =
							elevator_profiler->GetNextRefElevator();
					el->Move(profile_elevator);
				}

				elevatorTimer->Reset();

			}
		}
	}

}

void Elevator::EndElevatorThread() {

	ElevatorThread.~thread();

}

void Elevator::ZeroEncs() {

	if (zeroing_counter_e < 2) {
		talonElevator1->GetSensorCollection().SetQuadraturePosition(0, 0);//SetSelectedSensorPosition(0, 0, 0);
		zeroing_counter_e++;
	}
	else {
		is_elevator_init = true;
	}

}
