#include "CPollService.h"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

CPollService::CPollService(SOCKET socket)
{
	m_socket = socket;

	pollfd fd;
	memset(&fd, 0, sizeof(fd));

	fd.fd = socket;
	fd.events = POLLIN;

	m_pollfds.push_back(fd);
}

BYTE CPollService::Poll()
{
	if (m_pollfds.size() < 1)
		return POLLSERVICE_MAINSOCKETMISSING;

	if (poll(m_pollfds.data(), m_pollfds.size(), POLL_FOREVER) <= 0) 
	{
		printf(KRED "poll() error.\n" KNRM);
		return POLLSERVICE_POLLERROR;
	}

	// Means new connection(s) are incoming.
	if (m_pollfds[0].revents == POLLIN)
	{
		SOCKET new_socket = INVALID_SOCKET;
		while ((new_socket = accept(m_socket, NULL, NULL)) > INVALID_SOCKET)
		{
			// Make socket non blocking ??
			int nOn = 1;
			if (ioctl(new_socket, FIONBIO, (char *)&nOn) <= SOCKET_ERROR) {
				printf(KRED "Error making non-blocking file descriptor for %d.\n" KNRM, new_socket);
				close(new_socket);
				continue;
			}

			pollfd fd;
			memset(&fd, 0, sizeof(pollfd));

			fd.fd = new_socket;
			fd.events = POLLIN;

			m_pollfds.push_back(fd);

			m_fnOnNewConnection(new_socket);
		}

		if (errno != EWOULDBLOCK)
		{
			printf(KRED "accept() error.\n" KNRM);
			return POLLSERVICE_ACCEPTERROR;
		}
	}

	// No clients connected.
	if (m_pollfds.size() == 1)
		return POLLSERVICE_OK;

	// Iterate each clients to check for events.
	for (auto it = m_pollfds.begin()+1; it != m_pollfds.end(); )
	{
		// Nothing happened for this client.
		if ((*it).revents == 0)
		{
			++it;
			continue;
		}

		if ((*it).revents != POLLIN) 
		{
			printf(KRED "Error: revents = %d, client kicked.\n" KNRM, (*it).revents);
			m_fnOnCloseConnection((*it).fd);
			close((*it).fd);
			it = m_pollfds.erase(it);
			continue;
		}

		bool should_erase = false;

		while (true)
		{
			PACKETBUFFER buffer;
			memset(&buffer, 0, sizeof(PACKETBUFFER));

			int nLen = recv((*it).fd, &buffer, sizeof(PACKETBUFFER), 0);

			if (nLen == 0) {
				m_fnOnCloseConnection((*it).fd);
				close((*it).fd);
				should_erase = true;
				break;
			}

			if (nLen < 0 && errno != EWOULDBLOCK) {
				printf("recv() error.\n");
				return POLLSERVICE_RECVERROR;
			}

			if (nLen < 0)
				break;

			if (nLen > MAX_PACKET_LENGTH) {
				printf(KRED "Buffer nLen exceeded MAX_PACKET_LENGTH.\n");
				continue;
			}

			// Cut buffer into packets
			char *p = (char*)&buffer;
			while (nLen > 0 && nLen >= *(WORD*)p)
			{
				Packet packet;
				memset(&packet, 0, sizeof(Packet));
				memcpy(&packet, p, *(WORD*)p);

				m_fnOnIncomingPacket((*it).fd, packet);

				nLen -= *(WORD*)p;
				p += *(WORD*)p;
			}

			if (nLen != 0) {
				printf(KRED "ERROR: Some data was left after reading: %d.\n" KNRM, nLen);
			}
		}

		printf("while(true) ended\n");

		if (should_erase)
			it = m_pollfds.erase(it);
		else
			++it;
	}

	return POLLSERVICE_OK;
}
