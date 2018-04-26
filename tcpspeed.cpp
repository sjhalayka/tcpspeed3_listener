#include <winsock2.h>
#include <Ws2tcpip.h>
#pragma comment(lib, "ws2_32")

#include "socket.h"

#include <atomic>
using std::atomic_bool;

#include <vector>
using std::vector;

#include <thread>
using std::thread;
using std::this_thread::get_id;

#include <mutex>
using std::mutex;

#include <vector>
using std::vector;

#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
using std::cout;
using std::endl;
using std::string;
using std::istringstream;
using std::ostringstream;
using std::ios;

atomic_bool stop = false;

void thread_func(atomic_bool &stop, vector<string> &vs, mutex &m, 
				const SOCKET tcp_socket, const long unsigned int port_number)
{
	while (!stop)
	{
		const long unsigned int rx_buf_size = 8196;
		char rx_buf[8196];

		TCP_server ts;

		ts.init(tcp_socket, port_number);

		while (!stop)
		{
			if (true == ts.check_for_pending_connection())
			{
				//cout << "Connect" << endl;
				break;
			}
		}


		double record_bps = 0;
		long unsigned int temp_bytes_received = 0;
		double total_elapsed_time = 0;
		double last_reported_at_time = 0;
		long long unsigned int total_bytes_received = 0;
		long long unsigned int last_reported_total_bytes_received = 0;

		while (!stop)
		{

			auto start = std::chrono::system_clock::now();

			if (SOCKET_ERROR == (temp_bytes_received = ts.recv_data(rx_buf, rx_buf_size, 0)))
			{
				if (WSAEWOULDBLOCK != WSAGetLastError() && !stop)
				{
					m.lock();
					cout << "  Receive error " << WSAGetLastError() << endl;
					cout << "  Restarting server" << endl;
					m.unlock();
					break;
				}
			}
			else
			{
				total_bytes_received += temp_bytes_received;
			}

			auto end = std::chrono::system_clock::now();

			std::chrono::duration<double> elapsed_seconds = end - start;

			total_elapsed_time += elapsed_seconds.count();

			if (total_elapsed_time >= last_reported_at_time + 1)
			{
				long long unsigned int bytes_sent_received_between_reports = total_bytes_received - last_reported_total_bytes_received;

				//double bytes_per_second = static_cast<double>(bytes_sent_received_between_reports) / ((static_cast<double>(total_elapsed_ticks) - static_cast<double>(last_reported_at_ticks)) / 1000.0);
				double bytes_per_second = static_cast<double>(bytes_sent_received_between_reports) / (static_cast<double>(total_elapsed_time) - static_cast<double>(last_reported_at_time));

				if (bytes_per_second > record_bps)
					record_bps = bytes_per_second;

				last_reported_at_time = total_elapsed_time;
				last_reported_total_bytes_received = total_bytes_received;

				static const double mbits_factor = 8.0 / (1024.0 * 1024);

				std::thread::id this_id = get_id();
				ostringstream oss;
				oss << "  " << "Thread id: " << this_id << " - " << bytes_per_second * mbits_factor << " Mbit/s, Record: " << record_bps * mbits_factor << " Mbit/s";

				m.lock();
				vs.push_back(oss.str());
				m.unlock();

				if (0 == bytes_per_second)
				{
					if (!stop)
					{
						//cout << "  Connection throttled to death." << endl;
						m.lock();
						cout << "  Connection throttled to death. " << WSAGetLastError() << endl;
						cout << "  Restarting server" << endl;
						m.unlock();

						break;

						//return;
					}
				}
			}
		}
	}
}

void print_usage(void)
{
	cout << "  USAGE:" << endl;
	cout << "    tcpspeed PORT_NUMBER" << endl;

	cout << endl;
}

bool verify_port(const string &port_string, unsigned long int &port_number)
{
	for (size_t i = 0; i < port_string.length(); i++)
	{
		if (!isdigit(port_string[i]))
		{
			cout << "  Invalid port: " << port_string << endl;
			cout << "  Ports are specified by numerals only." << endl;
			return false;
		}
	}

	istringstream iss(port_string);
	iss >> port_number;

	if (port_string.length() > 5 || port_number > 65535 || port_number == 0)
	{
		cout << "  Invalid port: " << port_string << endl;
		cout << "  Port must be in the range of 1-65535" << endl;
		return false;
	}

	return true;
}

bool init_winsock(void)
{
	WSADATA wsa_data;
	WORD ver_requested = MAKEWORD(2, 2);

	if (WSAStartup(ver_requested, &wsa_data))
	{
		cout << "Could not initialize Winsock 2.2.";
		return false;
	}

	if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2)
	{
		cout << "Required version of Winsock (2.2) not available.";
		return false;
	}

	return true;
}

BOOL console_control_handler(DWORD control_type)
{
	stop = true;
	return TRUE;
}

bool init_options(const int &argc, char **argv, long unsigned int &port_number)
{
	if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)console_control_handler, TRUE))
	{
		cout << "  Could not add console control handler." << endl;
		return false;
	}

	if (!init_winsock())
		return false;

	string port_string = "";

	if (2 == argc)
	{
		port_string = argv[1];
	}
	else
	{
		print_usage();
		return false;
	}

	cout.setf(ios::fixed, ios::floatfield);
	cout.precision(2);

	return verify_port(port_string, port_number);
}

void cleanup(void)
{
	// if the program was aborted, flush cout and print a final goodbye
	if (stop)
	{
		cout.flush();
		cout << endl << "  Stopping." << endl;
	}

	// shut down winsock
	WSACleanup();

	// remove the console control handler
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)console_control_handler, FALSE);
}


int main(int argc, char **argv)
{
	cout << endl << "tcpspeed 3.1 - TCP speed tester" << endl << "Copyright 2018, Shawn Halayka" << endl << endl;

	size_t num_threads = 10;
	long unsigned int port_number = 0;

	// initialize winsock and all of the program's options
	if (!init_options(argc, argv, port_number))
	{
		cleanup();
		return 1;
	}

	cout << "  Listening on TCP port " << port_number << " - CTRL+C to exit." << endl;

	mutex m;
	vector<string> vs;
	vector<thread> threads;

	TCP_server s;

	if (false == s.init(port_number))
		return 2;

	for (size_t i = 0; i < num_threads; i++)
		threads.push_back(thread(thread_func, ref(stop), ref(vs), ref(m), s.get_tcp_socket(), port_number));

	while (!stop)
	{
		m.lock();

		for (vector<string>::const_iterator ci = vs.begin(); ci != vs.end(); ci++)
			cout << *ci << endl;

		vs.clear();

		m.unlock();
	}

	for (size_t i = 0; i < num_threads; i++)
		threads[i].join();

	cleanup();

	return 0;
}