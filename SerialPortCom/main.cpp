#include <Windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <mariadb/mysql.h>
#include <DeviceINQ.h>
#include <BTSerialPortBinding.h>
#include <BluetoothException.h>
void showError(MYSQL* mysql)
{
	std::cerr << "ERROR(" <<
		mysql_errno(mysql) << ")[" <<
		mysql_sqlstate(mysql) << "] \"" <<
		mysql_error(mysql) << "\"\n";
}
int main(int argc, char** argv)
{
	if (argc != 7)
	{
		std::cout << 
			"USAGE: SerialPortCom.exe [sqlAddress] [sqlPort] [sqlUser] [sqlPass] [sqlDb] [sensorBluetoothName]\n";
		system("pause");
		return EXIT_FAILURE;
	}
	/// for (int c = 0; c < argc; c++)
	/// {
	/// 	std::cout << "argv[" << c << "]=\"" << argv[c] << "\"\n";
	/// }
	// SQL stuff //
	std::string sqlAddress = argv[1];
	UINT16      sqlPort    = atoi(argv[2]);
	std::string sqlUser    = argv[3];
	std::string sqlPass    = argv[4];
	std::string sqlDb      = argv[5];
	MYSQL* mysql;
	mysql = mysql_init(nullptr);
	std::cout << "connecting to SQL server...\n";
	while (!mysql_real_connect(mysql, sqlAddress.c_str(), sqlUser.c_str(),
			sqlPass.c_str(), sqlDb.c_str(), sqlPort, nullptr, 0))
	{
		showError(mysql);
	}
	std::cout << "SQL server connected!\n";
	// Superior (hopefully) Bluetooth Serial library stuff //
	std::cout << "Querying synced bluetooth device list...\n";
	std::unique_ptr<DeviceINQ> inq(DeviceINQ::Create());
	std::vector<device> devices = inq->Inquire();
	BTSerialPortBinding* serialPortBinding = nullptr;
	for (size_t d = 0; d < devices.size(); d++)
	{
		const auto& device = devices[d];
		std::cout <<"device["<<d<<"]=\""<< device.name << "\"@" << device.address << std::endl;
		if (device.name._Equal(argv[6]))
		{
			try
			{
				serialPortBinding = BTSerialPortBinding::Create(device.address, 1);
				serialPortBinding->setTimoutRead(8);
			}
			catch (const BluetoothException& bte)
			{
				std::cerr << ": failed to create serial port binding!";
				std::cerr << " BluetoothException= \"" << bte.what() << "\"\n";
				system("pause");
				return EXIT_FAILURE;
			}
		}
	}
	if (!serialPortBinding)
	{
		std::cerr << ": could not find device \"" << argv[6] << "\"\n";
		system("pause");
		return EXIT_FAILURE;
	}
	// Query device & send data to SQL server! //
	std::string cumulativeBuffer;
	while(true)
	{
		///TODO: reconnect to the SQL server if the connection is lost
		int numBytesRead;
		static const size_t BUFFER_SIZE = 1024;
		char buffer[BUFFER_SIZE];
		bool dataIsAvailable = false;
		bool portConnected   = true;
		try
		{
			dataIsAvailable = serialPortBinding->IsDataAvailable();
			std::cout << "serialPortBinding data available...\n";
		}
		catch (const BluetoothException& bte)
		{
			std::cerr << "serialPortBinding failed to check data availability!";
			std::cerr << " BluetoothException= \"" << bte.what() << "\"\n";
			portConnected = false;
		}
		if (!portConnected)
		{
			try
			{
				serialPortBinding->Connect();
				std::cout << "serialPortBinding reconnected...\n";
				portConnected = true;
			}
			catch (const BluetoothException& bte)
			{
				std::cerr << "serialPortBinding failed to connect!";
				std::cerr << " BluetoothException= \"" << bte.what() << "\"\n";
				portConnected = false;
			}
		}
		if (!portConnected)
		{
			continue;
		}
		try
		{
			std::cerr << "Reading... ";
			numBytesRead = serialPortBinding->Read(buffer, BUFFER_SIZE);
			std::cerr << "Done!\n";
		}
		catch (const BluetoothException& bte)
		{
			std::cerr << "serialPortBinding Read failed!";
			std::cerr << " BluetoothException= \"" << bte.what() << "\"\n";
			continue;
		}
		if (numBytesRead <= 0)
		{
			continue;
		}
		cumulativeBuffer += std::string{ buffer, size_t(numBytesRead) };
		const std::string strData = cumulativeBuffer;
		// Step 1) extract the data from the buffer //
		const size_t humidIndex    = strData.find("H=");
		const size_t humidIndexEnd = strData.find("%");
		const size_t tempIndex     = strData.find("T=");
		const size_t tempIndexEnd  = strData.find("C");
		const size_t voltIndex     = strData.find("V=");
		const size_t voltIndexEnd  = strData.find("U");
		if (humidIndex    == std::string::npos ||
			humidIndexEnd == std::string::npos ||
			tempIndex     == std::string::npos || 
			tempIndexEnd  == std::string::npos || 
			voltIndex     == std::string::npos || 
			voltIndexEnd  == std::string::npos)
		{
			static const size_t ARBITRARILY_LARGE_CUMULATIVE_BUFFER_SIZE = 1024;
			if (cumulativeBuffer.size() >= ARBITRARILY_LARGE_CUMULATIVE_BUFFER_SIZE)
			{
				std::cerr << "cumulativeBuffer overflowed! reset to empty string\n";
				std::cerr << "\tcumulativeBuffer=\""<<cumulativeBuffer<<"\"\n";
				cumulativeBuffer = "";
			}
			std::cout << "cumulativeBuffer insufficient data:\"" << cumulativeBuffer << "\"\n";
			continue;
		}
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
		// erase cumulative buffer up util the end of the data //
		cumulativeBuffer = cumulativeBuffer.substr(voltIndexEnd + 1);
	}
	// CLEAN UP //
	if (serialPortBinding)
	{
		serialPortBinding->Close();
		delete serialPortBinding;
		serialPortBinding = nullptr;
	}
	mysql_close(mysql);
	system("pause");
	return EXIT_SUCCESS;
}
