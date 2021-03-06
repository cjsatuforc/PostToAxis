#include "PTASocket.h"
#include <iostream>


PTASocket::PTASocket()
{
	_ui = Application::get()->userInterface();
}


PTASocket::~PTASocket()
{
	if (_socket != INVALID_SOCKET)
		disconnect();
}

bool PTASocket::connectTo(const std::string ipAddr, const std::string port)
{
	fd_set fdset;

	int err;
	err = WSAStartup(MAKEWORD(2, 2), &_wsaData);

	if (err != 0)
	{
		_ui->messageBox("Unable to start winsock");
		return false;
	}

	_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (_socket == INVALID_SOCKET)
	{
		_ui->messageBox("Unable to create socket object");
		WSACleanup();
		return false;
	}

	// Make socket non blocking
	int non_blocking = 1;
	err = ioctlsocket(_socket, FIONBIO, (u_long *)&non_blocking);
	
	if (err == SOCKET_ERROR) {
		_ui->messageBox("setsockopt returned:\n" + getErrorString(WSAGetLastError()));
		disconnect();

		return false;
	}
/*
	int timeout = 10000;
	setsockopt(_socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
	setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
*/

	_addr.sin_family = AF_INET;
	_addr.sin_port = htons(std::stoi(port));
	inet_pton(AF_INET, ipAddr.c_str(), &_addr.sin_addr);




	timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;

	do {
		FD_ZERO(&fdset);
		FD_SET(_socket, &fdset);

		err = connect(_socket, (sockaddr *)&_addr, sizeof(sockaddr_in));

		if (err == SOCKET_ERROR)
		{
			int errCode = WSAGetLastError();

			if (errCode != WSAEWOULDBLOCK) {
				_ui->messageBox("connect returned:\n" + getErrorString(errCode));
				disconnect();
				return false;
			}
		}

		int selectReturn = select(_socket + 1, NULL, &fdset, NULL, &tv);
		int len = sizeof(int);

		// Connection timed out, do loop again
		if (selectReturn == 0)
			continue;

		/* Socket error or connection attempt timed out */
		if (selectReturn == SOCKET_ERROR)
		{
			errno = WSAGetLastError();

			_ui->messageBox("Error\n" + getErrorString(errno));
			disconnect();
			return false;
		}

		// If code reaches here, connection has not timed out nor errored out.
		break;
	}
	while (_ui->messageBox("Connection timed out. Retry connection?", "Connection attempt timed out", YesNoButtonType) != DialogNo);
	/*
	non_blocking = 0;
	setsockopt(_socket, SOL_SOCKET, FIONBIO, (char *)&non_blocking, sizeof(int));
	*/
	return true;
}

void PTASocket::disconnect()
{
	if (_socket != INVALID_SOCKET)
		closesocket(_socket);

	WSACleanup();
}

int PTASocket::sendPacket(PTAPacket *packet)
{
	//	packet->size = sizeof(PTAPacket) + packet->dataLen;
	//	packet->size = sizeof(PTAPacket) + ntohl(packet->dataLen);

	uint32_t packetType = htonl(packet->packetType);
	size_t packetSize = htonl(packet->size);
	size_t dataLen = htonl(packet->dataLen);

	if (send((char *)&packet->identifier, 4) < 0)
		return -1;
	
	if (send((char *)&packetSize, 4) < 0)
		return -1;

	if (send((char *)&packetType, 4) < 0)
		return -1;

	if (send((char *)&packet->md5, 32) < 0)
		return -1;

	if (send((char *)&dataLen, 4) < 0)
		return -1;

	if (send((char *)packet->data, packet->dataLen) < 0)
		return -1;

	return 1;
}
	

int PTASocket::send(const char *buffer, int len) {
	if (buffer == nullptr || len < 1)
	{
		_ui->messageBox("buffer == nullptr ||len < 1)");
		return -1;
	}

	int bytesSent = 0;

	// Check if we can send the whole buffer at once.
	while (len > 0) {
		int sent = ::send(_socket, buffer, len, 0);
		if (sent == SOCKET_ERROR)
			return -1;

		buffer += sent;
		len -= sent;
		bytesSent += sent;
	}

	return bytesSent;
}

int PTASocket::sendFile(const char *path)
{
	std::ifstream fileToSend(path, std::ifstream::binary | std::ifstream::ate);

	if (fileToSend.is_open())
	{
		int fileSize = fileToSend.tellg();
		fileToSend.seekg(fileToSend.beg);

		char *buffer = new char[fileSize];

		fileToSend.read(buffer, fileSize);
		fileToSend.close();

		int sent = send(buffer, fileSize);

		if (sent < 0)
			return -1;

		return sent;
	}

	return -1;
}

char *PTASocket::receive(int len) {
	int totalBytes = 0;

	char *recvBuffer = new char[len];

	while (totalBytes < len)
	{
		char buffer[BUFFER_SIZE];

		int bytesReceived = recv(_socket, (char *)&buffer, len < BUFFER_SIZE ? len : BUFFER_SIZE, 0);

		if (bytesReceived == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				break;

			_ui->messageBox("receiveAll() Socket error: " + std::to_string(WSAGetLastError()));

			return nullptr;
		}
		else if (bytesReceived < 0) {
			_ui->messageBox("Connection closed");
			return nullptr;
		}
		else {
			memcpy(recvBuffer + totalBytes, &buffer, bytesReceived);
			totalBytes += bytesReceived;
		}
	}

	return recvBuffer;
}

char *PTASocket::receiveAll(int *len) {
	unsigned long totalBytes = 0;
	unsigned long recvBufferLen = BUFFER_SIZE;

	char *recvBuffer = new char[recvBufferLen];

	while (true)
	{
		char buffer[BUFFER_SIZE];

		int bytesReceived = recv(_socket, (char *)&buffer, BUFFER_SIZE, 0);

		if (bytesReceived == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				break;
			_ui->messageBox("receiveAll() Socket error: " + std::to_string(WSAGetLastError()));
			return nullptr;
		}
		else if (bytesReceived < 0) {
			_ui->messageBox("Connection closed");
			return nullptr;
		}
		else {
			if (recvBufferLen < totalBytes + bytesReceived)
			{
				char *newArr = new char[totalBytes + bytesReceived + 1];
				memcpy(newArr, recvBuffer, recvBufferLen);
				delete[]recvBuffer;
				recvBuffer = newArr;
				recvBufferLen = totalBytes + bytesReceived;
			}

			memcpy(recvBuffer + totalBytes, &buffer, bytesReceived);
			totalBytes += bytesReceived;
			//			_ui->messageBox("Received: " + std::to_string(bytesReceived));

			if (bytesReceived < BUFFER_SIZE) {
				recvBuffer[totalBytes + 1] = '\0';
				break;
			}
		}
	}

	*len = totalBytes;

	return recvBuffer;
}

std::string PTASocket::getErrorString(int errorCode)
{
	char msgBuff[256];

	msgBuff[0] = '\0';

	FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		msgBuff,
		sizeof(msgBuff),
		NULL
	);
	if (!*msgBuff)
		return std::to_string(errorCode);

	return std::string(msgBuff) + "(" + std::to_string(errorCode) + ")";
}