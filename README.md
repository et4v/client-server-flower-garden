# 🌸 The Blooming Garden  
## A Mini-Distributed Control System

## Overall Concept
I built a small simulated system that behaves like a very simple distributed robotic garden. Each flower operates as its own TCP client and contains several petals (up to eight) that move incrementally over time.

The server acts as a central controller and sends high-level commands to all flowers (or individual ones). Each client handles its own timing and animation, similar to how small embedded boards control their own motors while listening to commands from a main controller.

This design closely mirrors real robotics and embedded systems architectures while keeping the code clean and modular.

---

## Client Responsibilities
Each client program represents a single flower. After connecting to the server, each flower:

- Waits for server commands  
- Simulates petal motion and bloom behavior  
- Updates petal angles gradually over time  
- Sends status messages back to the server  
- Prints its own movement for visualization and debugging  
- Safely closes and terminates when receiving a TERMINATE command  

---

## Server Responsibilities
The server acts as the central controller for the entire garden. It:

- Accepts and tracks multiple flower clients simultaneously  
- Stores client names, statuses, and connection information  
- Receives and displays client status updates  
- Sends commands such as `OPEN`, `CLOSE`, `SEQ1`, `SEQ2`, and `TERMINATE`  
- Executes a `BLOOM` command that triggers randomized, staggered bloom timing  
- Safely shuts down the system by terminating all connected flowers  

---

## Why the Files Are Split
Flower logic for petals, angles, motion, and sequences is implemented in `flower.c` and `flower.h`. Networking and threading logic lives in the server and client source files.

This separation keeps movement and math logic independent from socket communication. If the system were ever implemented physically, the flower behavior could be ported to a microcontroller without restructuring the overall architecture.

---

## Programming Language and Libraries
- **Language:** C  
- **Libraries:** CS:APP networking library  
- **Build System:** Makefile (builds both server and client programs and is included!) 

---

## How to Run

### For MacOS/Linux
1. Use a Unix-based operating system (macOS or Linux)
2. Ensure a C compiler and `make` are available
3. Build the project using the provided Makefile
4. Run the server program first using ./garden_server <port>
5. Run one or more flower client programs in separate terminals using ./flower_client <server_host> <port> <flower_name> <num_petals>
6. Enter commands using the server terminal

### Windows
Windows does not natively support POSIX Makefiles, but the project can still be run by using Windows Subsystem for Linux (WSL) or some kind of Unix-compatible environment such as MSYS2 or MinGW.

### Running Across Multiple Devices (So Cool)

To run the garden server and flower clients on different devices, follow these steps:

- Choose one device to act as the **garden server**
- Start the server and keep it running for the duration of the session
- Identify the **IP address or hostname** of the server device
- On each flower device, ensure it can reach the server over the network
- Configure each flower client to connect using the server’s IP address in the terminal command
- Start flower clients only after the server is running

Notes:
- All devices should be on the same local network or otherwise properly routed to connect
- Firewalls or restricted networks may block connections between devices, so check that first if there issues
