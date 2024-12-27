# B11902147 CSIE3 曹鈞皓 B11902071 CSIE3 楊子榮

# Secure Chat Application with Video Streaming

This is a secure chat application with video streaming capabilities implemented in C.

## Compilation Guide

### Prerequisites

Before compiling, make sure you have the following libraries installed:

- OpenSSL development libraries
- GTK3 development libraries 
- FFmpeg development libraries
- SDL2 development libraries
- pkg-config


On Ubuntu/Debian, you can install them with:
```bash
sudo apt-get update
sudo apt-get install -y \
    libssl-dev \
    libgtk-3-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libsdl2-dev \
    pkg-config
```

### SSL Certificate Generation

Before running the application, you need to generate SSL certificates. Use the following commands:

```bash
# Generate private key
openssl genrsa -out server.key 2048

# Generate certificate signing request (CSR)
openssl req -new -key server.key -out server.csr

# Generate self-signed certificate (valid for 365 days)
openssl x509 -req -days 365 -in server.csr -signkey server.key -out server.crt

# Clean up CSR as it's no longer needed
rm server.csr
```

When generating the CSR, you'll be asked several questions. You can use the following example values:
- Country Name: TW
- State/Province: Taiwan
- Locality: Taipei
- Organization: NTU
- Organizational Unit: CS
- Common Name: localhost
- Email Address: [your email]

The generated files (`server.key` and `server.crt`) should be placed in the same directory as the server executable.


## Usage Guide

### Starting the Application

1. First, start the server:
```bash
./server
```

2. Then, start the client:
```bash
./client
```

3. When starting the client, you'll be asked to enter a port number for receiving direct messages. Choose any available port number (e.g., 8000).

### Basic Operations

#### Registration
1. From the main menu, select option `1` (Register)
2. Enter your desired username (max 15 characters)
3. If the username is available, you'll see a "Register Success!" message

#### Login
1. From the main menu, select option `2` (Login)
2. Enter your registered username
3. Upon successful login, you'll see a "Login Success!" message

#### Exit
1. From the main menu, select option `3` (Exit)
2. The client will close the connection and exit
3. You can also use Ctrl+C to force exit at any time

### Chat Features

#### View Online Users
1. Select option `1` (Show online users)
2. You'll see a list of all registered users with their status:
   - `*`: User is online
   - `YOU`: Your own username
   - No mark: User is offline

#### Send Broadcast Message
1. Select option `2` (Broadcast)
2. Enter the target user's ID (shown in the user list)
3. Type your message
4. The message will be sent to the specified user through the server

#### Direct Message
1. Select option `3` (Chat)
2. Enter the target user's ID
3. Type your message
4. The message will be sent directly to the user's client

#### File Transfer
1. Select option `4` (File transfer)
2. Enter the target user's ID
3. Enter the filename to send
4. The recipient will get a GTK dialog asking to accept/reject the file
5. If accepted, the file will be transferred

#### Video Streaming
1. Select option `5` (Stream video)
2. Enter the video filename (e.g., "test.mp4")
3. The video will be streamed and displayed in a new window using SDL2

#### Logout
1. Select option `6` (Logout)
2. You'll be returned to the main menu

### Additional Features

#### Message Types
- Broadcast messages are shown with "Sent by Relay Message"
- Direct messages are shown with "Sent by Direct Message"
- File transfers show a progress indicator

#### GUI Elements
- File transfer requests appear in a GTK window
- Video streams appear in an SDL window
- Messages are color-coded for better readability

### Error Handling
- If a user is offline, you'll receive an "User offline" message
- Failed file transfers will show appropriate error messages
- Invalid commands will prompt you to try again

### Security Features
- All communications are encrypted using SSL/TLS
- File transfers are secure and require recipient approval
- Direct messages use separate secure connections

### Tips
- Keep the server running at all times
- Make sure video files are in the correct directory
- For best performance, use video files in H.264 format
- The client must have read/write permissions in the current directory for file transfers


