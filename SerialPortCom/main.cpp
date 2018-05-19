#include <Windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <mariadb/mysql.h>
void showError(MYSQL* mysql)
{
	std::cerr << "ERROR(" <<
		mysql_errno(mysql) << ")[" <<
		mysql_sqlstate(mysql) << "] \"" <<
		mysql_error(mysql) << "\"\n";
}
int main(int argc, char** argv)
{
	for (int c = 0; c < argc; c++)
	{
		std::cout << "argv[" << c << "]=\"" << argv[c] << "\"\n";
	}
	// SQL stuff //
	std::string sqlAddress = argv[1];
	UINT16      sqlPort    = atoi(argv[2]);
	std::string sqlUser    = argv[3];
	std::string sqlPass    = argv[4];
	std::string sqlDb      = argv[5];
	MYSQL* mysql;
	mysql = mysql_init(nullptr);
	if (!mysql_real_connect(mysql, sqlAddress.c_str(), sqlUser.c_str(),
			sqlPass.c_str(), sqlDb.c_str(), sqlPort, nullptr, 0))
	{
		showError(mysql);
	}
	//mysql_close(mysql);
	// COM port stuff //
	BOOL success;
	HANDLE serialHandle;
	serialHandle = CreateFile("COM5", GENERIC_READ | GENERIC_WRITE,
		0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (serialHandle == INVALID_HANDLE_VALUE)
	{
		std::cerr << "Failed to CreateFile for com port!\n";
		return EXIT_FAILURE;
	}
	DCB serialParams = { 0 };
	serialParams.DCBlength = sizeof(serialParams);
	success = GetCommState(serialHandle, &serialParams);
	if (!success)
	{
		std::cerr << "Failed to GetCommState!  GetLastError()=" << GetLastError();
		return EXIT_FAILURE;
	}
	serialParams.BaudRate = CBR_9600;
	serialParams.ByteSize = 8;
	serialParams.StopBits = ONESTOPBIT;
	serialParams.Parity   = NOPARITY;
	success = SetCommState(serialHandle, &serialParams);
	if (!success)
	{
		std::cerr << "Failed to SetCommState!  GetLastError()=" << GetLastError();
		return EXIT_FAILURE;
	}
	success = GetCommState(serialHandle, &serialParams);
	COMMTIMEOUTS timeouts = { 0 };
	timeouts.ReadIntervalTimeout         = 50;
	timeouts.ReadTotalTimeoutConstant    = 50;
	timeouts.ReadTotalTimeoutMultiplier  = 50;
	timeouts.WriteTotalTimeoutConstant   = 50;
	timeouts.WriteTotalTimeoutMultiplier = 10;
	success = SetCommTimeouts(serialHandle, &timeouts);
	if (!success)
	{
		std::cerr << "Failed to SetCommTimeouts!  GetLastError()=" << GetLastError();
		return EXIT_FAILURE;
	}
	while(true)
	{
		DWORD numBytesRead;
		static const DWORD BUFFER_SIZE = 1024;
		BYTE buffer[BUFFER_SIZE];
		if (ReadFile(serialHandle, LPVOID(buffer), BUFFER_SIZE, &numBytesRead, NULL))
		{
			// Step 1) extract the data from the buffer //
			//std::stringstream ss;
			//ss << buffer;
			const std::string strData{ (const char*)buffer, numBytesRead };
			const size_t humidIndex    = strData.find("H=");
			const size_t humidIndexEnd = strData.find("%");
			const size_t tempIndex     = strData.find("T=");
			const size_t tempIndexEnd  = strData.find("C");
			const size_t voltIndex     = strData.find("V=");
			const size_t voltIndexEnd  = strData.find("U");
			std::stringstream queryCols;
			std::stringstream queryValues;
			float humidity;
			float temperatureCelsius;
			float voltage;
			bool doQuery = true;
			auto extractFloat = [&](size_t index, size_t indexEnd, float& outFloat)->void
			{
				if (index != std::string::npos && indexEnd != std::string::npos)
				{
					const size_t indexReal = index + 2;
					std::string strFloat = strData.substr(indexReal, indexEnd - indexReal);
					outFloat = float(atof(strFloat.c_str()));
				}
				else
				{
					doQuery = false;
				}
			};
			extractFloat(humidIndex, humidIndexEnd, humidity);
			extractFloat(tempIndex , tempIndexEnd , temperatureCelsius);
			extractFloat(voltIndex , voltIndexEnd , voltage);
			// Step 2) Build and execute the sql query //
			if (doQuery)
			{
				std::stringstream ssQuery;
				ssQuery << "INSERT INTO `sensor-0`(`datetime`, `temperature-celsius`, `humidity`, `voltage`)";
				ssQuery << " VALUES (CURRENT_TIMESTAMP()," << temperatureCelsius;
				ssQuery << "," << humidity << "," << voltage << ")";
				std::cout << "query=\"" << ssQuery.str() << "\"\n";
				if (mysql_real_query(mysql, ssQuery.str().c_str(), (unsigned long)ssQuery.str().size()))
				{
					showError(mysql);
				}
			}
			else
			{
				std::cout << "Serial Data Read! data=\"" << strData << "\"\n";
			}
		}
	}
	CloseHandle(serialHandle);
	return EXIT_SUCCESS;
}
