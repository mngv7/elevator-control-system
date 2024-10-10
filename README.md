# Elevator System Simulation

## Overview

This project implements a multi-component elevator system for a multi-storey building. The components communicate over TCP-IP and POSIX shared memory. The key components include:

- **Car**: Manages the functionality of a single elevator car.
- **Controller**: Schedules all elevators in the network.
- **Call Pad**: Simulates user requests for elevator service from each floor.
- **Internal Controls**: Simulates the buttons inside an elevator car.
- **Safety System**: Monitors the elevator's internal conditions and activates emergency protocols when necessary.

## Components

### 1. Car
- **Function**: Controls the operation of an individual elevator car.
- **Shared Memory**: Each car has a dedicated shared memory segment (e.g., `/carA`, `/carB`, etc.) that stores car status and controls.

### 2. Controller
- **Function**: Acts as the central scheduler for the elevator system.
- **Communication**: Functions as a TCP-IP server on port 3000.

### 3. Call Pad
- **Function**: Simulates the device on each floor where users request elevators.
- **Communication**: Connects to the controller, sending the current and requested floor.

### 4. Internal Controls
- **Function**: Simulates buttons inside the elevator car for opening/closing doors and emergency functions.
- **Communication**: Interacts with the shared memory segment of the associated car.

### 5. Safety System
- **Function**: Monitors conditions inside the elevator for safety.
- **Standards**: Developed following MISRA C guidelines due to safety-critical nature.

## Scenario

- Floors are labeled numerically (1-10) and with a 'B' prefix for basement levels (e.g., B1, B2). The range is from B99 to 999.
- Each elevator car operates within its designated shaft and cannot move between them.
- Call pads use a destination dispatch system, with each call pad linked to the elevators serving that floor.

## Shared Memory Structure

Each elevator car uses the following shared memory structure:

```c
typedef struct {
  pthread_mutex_t mutex;           // Lock for accessing struct contents
  pthread_cond_t cond;             // Signaled when contents change
  char current_floor[4];           // Current floor (B99-B1, 1-999)
  char destination_floor[4];       // Destination floor (B99-B1, 1-999)
  char status[8];                  // Elevator status (e.g., "Open", "Closed")
  uint8_t open_button;             // Open doors button pressed
  uint8_t close_button;            // Close doors button pressed
  uint8_t door_obstruction;        // Obstruction detected
  uint8_t overload;                // Overload detected
  uint8_t emergency_stop;          // Emergency stop button pressed
  uint8_t individual_service_mode; // In individual service mode
  uint8_t emergency_mode;          // In emergency mode
} car_shared_mem;
```

### Elevator Status Values
- **"Opening"**: Doors are opening.
- **"Open"**: Doors are open.
- **"Closing"**: Doors are closing.
- **"Closed"**: Doors are shut.
- **"Between"**: Car is between floors.

### Access Rules
- Always acquire the mutex when reading or writing data in the shared memory segment.
- Signal the condition variable (using broadcast) after changing data.

## TCP-IP Communication

### Controller
- Runs as a TCP-IP server on port 3000.
- Handles requests from the call pad and connections from elevator cars.

### Call Pad
- Connects to the controller to request elevator service.
- Sends the current and requested floor, receiving back the dispatched elevator information.

### Car
- Connects to the controller and maintains this connection while operating.
- Provides status updates and receives commands from the controller.

### Message Protocol
- Each message begins with a 32-bit unsigned integer (in network byte order) indicating the number of bytes in the following ASCII string (not NUL-terminated).

## Running the Components

1. Start the **controller** to listen for incoming connections.
2. Launch the **call pad** and select the desired floor.
3. Start the **car** components to simulate the elevator operation.
4. Use the **internal controls** to test button functions within the car.
5. Monitor the **safety system** for emergency conditions.

## Development Standards

- The **safety system** component must adhere to MISRA C guidelines due to its critical nature in ensuring the safety of elevator operations.
