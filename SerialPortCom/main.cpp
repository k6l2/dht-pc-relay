///TODO: some kind of log file maybe?... 
///	or maybe that should just be done at the command line...
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
// Returns true if valid data string exists in the "inString"
//	false otherwise (could not extract valid data)
bool extractData(const std::string& inString, int& outSensorId, float& outHumidity,
	float& outTempCelsius, float& outVolt, std::string& outStrAck, uint64_t& outAck,
	size_t& outDataEnd)
{
	//	valid data string format: 
	//		DHT{[INT:sensorId],[FLOAT:humidity],[FLOAT:temp],
	//		[FLOAT:voltage],[uint64-base16:ack]}
	const size_t dataBegin = inString.find("DHT{");
	outDataEnd             = inString.find("}", dataBegin);
	if (dataBegin  == std::string::npos ||
		outDataEnd == std::string::npos)
	{
		return false;
	}
	if (inString.find("{", dataBegin + 4) < outDataEnd)
	{
		// some weird corruption of data going on here...
		return false;
	}
	// extract sensorId //
	const size_t idEnd = inString.find(",", dataBegin);
	if (idEnd >= outDataEnd)
	{
		return false;
	}
	const std::string strSensorId =
		inString.substr(dataBegin + 4, (idEnd - dataBegin) - 4);
	outSensorId = atoi(strSensorId.c_str());
	// extract relative humidity //
	const size_t humidityEnd = inString.find(",", idEnd + 1);
	if (humidityEnd >= outDataEnd)
	{
		return false;
	}
	const std::string strRelativeHumidity =
		inString.substr(idEnd + 1, (humidityEnd - idEnd) - 1);
	outHumidity = float(atof(strRelativeHumidity.c_str()));
	// extract temperature (C) //
	const size_t celsiusEnd = inString.find(",", humidityEnd + 1);
	if (celsiusEnd >= outDataEnd)
	{
		return false;
	}
	const std::string strCelsius =
		inString.substr(humidityEnd + 1, (celsiusEnd - humidityEnd) - 1);
	outTempCelsius = float(atof(strCelsius.c_str()));
	// extract voltage //
	const size_t voltageEnd = inString.find(",", celsiusEnd + 1);
	if (voltageEnd >= outDataEnd)
	{
		return false;
	}
	const std::string strVoltage =
		inString.substr(celsiusEnd + 1, (voltageEnd - celsiusEnd) - 1);
	outVolt = float(atof(strVoltage.c_str()));
	// extract acknowledgement string, convert from ascii-encoding to a # //
	outStrAck = inString.substr(voltageEnd + 1, (outDataEnd - voltageEnd) - 1);
	outAck = strtoul(outStrAck.c_str(), nullptr, 16);
	return true;
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
	std::cout << "Connecting to SQL server... ";
	while (!mysql_real_connect(mysql, sqlAddress.c_str(), sqlUser.c_str(),
			sqlPass.c_str(), sqlDb.c_str(), sqlPort, nullptr, 0))
	{
		showError(mysql);
	}
	std::cout << "Connected!\n";
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
				std::cout << "Device name matches argument[6]! Binding... ";
				serialPortBinding = BTSerialPortBinding::Create(device.address, 1);
				std::cout << "Success!\n";
				serialPortBinding->setTimoutRead(1);
			}
			catch (const BluetoothException& bte)
			{
				std::cerr << "FAILED! BluetoothException= \"" << bte.what() << "\"\n";
				system("pause");
				return EXIT_FAILURE;
			}
		}
	}
	if (!serialPortBinding)
	{
		std::cerr << " could not find device \"" << argv[6] << "\"\n";
		system("pause");
		return EXIT_FAILURE;
	}
	//serialPortBinding->Connect();
	///while (true)
	///{
	///}
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
			std::cout << "serialPortBinding checking data available... ";
			dataIsAvailable = serialPortBinding->IsDataAvailable();
			std::cout << "Done!\n";
		}
		catch (const BluetoothException& bte)
		{
			std::cerr << "FAILED! BluetoothException= \"" << bte.what() << "\"\n";
			portConnected = false;
		}
		if (!portConnected)
		{
			try
			{
				std::cout << "serialPortBinding reconnecting... ";
				serialPortBinding->Connect();
				std::cout << "Connected!\n";
				portConnected = true;
			}
			catch (const BluetoothException& bte)
			{
				std::cerr << "FAILED! BluetoothException= \"" << bte.what() << "\"\n";
				portConnected = false;
			}
		}
		if (!portConnected)
		{
			continue;
		}
		try
		{
			std::cout << "serialPortBinding Reading... ";
			numBytesRead = serialPortBinding->Read(buffer, BUFFER_SIZE);
			std::cout << "Done!\n";
		}
		catch (const BluetoothException& bte)
		{
			std::cerr << "FAILED! BluetoothException= \"" << bte.what() << "\"\n";
			continue;
		}
		if (numBytesRead <= 0)
		{
			continue;
		}
		cumulativeBuffer += std::string{ buffer, size_t(numBytesRead) };
		const std::string strData = cumulativeBuffer;
		// Step 1) extract the data from the buffer //
		int sensorId;
		float humidity, temperatureCelsius, voltage;
		std::string strAck;
		uint64_t ack;
		size_t dataEnd;
		const bool validDataExtracted = extractData(strData, 
			sensorId, humidity, temperatureCelsius, voltage, strAck, ack, dataEnd);
		if (!validDataExtracted)
		{
			std::cout << "invalid data=\"" << strData << "\"\n";
			if (dataEnd != std::string::npos)
			{
				// we found data, but it is corrupted somehow, so just discard it!
				std::cout << "Discarding corrupted data! cumulativeBuffer=\"" <<
					cumulativeBuffer << "\"\n";
				cumulativeBuffer = cumulativeBuffer.substr(dataEnd + 1);
			}
			continue;
		}
		/// DEBUG /////////////////////////////////
		std::cout << "sensor[" << sensorId << "] rh=" << humidity <<
			"% t=" << temperatureCelsius << "C volts=" << voltage <<
			" ACK=" << strAck <<"("<<ack<<")\n";
		// We now have the data, send the ack string back to the sensor //
		//	each time we write data to the serial port, we cut off the characters that
		//	were written from strAck successfully
		while (!strAck.empty())
		{
			bool sendFailed = false;
			try
			{
				std::cout << "serialPortBinding Sending ACK... ";
				const int bytesWritten = 
					serialPortBinding->Write(strAck.c_str(), (int)strAck.size());
				std::cout << "Done! wrote=\""<<strAck.substr(0, bytesWritten)<<"\"\n";
				// erase the part of the ack string that we successfully wrote
				strAck = strAck.substr(bytesWritten);
			}
			catch (const BluetoothException& bte)
			{
				std::cerr << "FAILED! BluetoothException= \"" << bte.what() << "\"\n";
				sendFailed = true;
			}
			if (sendFailed)
			{
				// abort trying to send the rest of the ack if serial protocol fails
				break;
			}
		}
		// Step 2) Build and execute the sql query //
		std::stringstream ssQuery;
		ssQuery << "INSERT INTO `sensor-" << sensorId;
		ssQuery << "`(`datetime`, `temperature-celsius`, `humidity`, `voltage`)";
		ssQuery << " VALUES (CURRENT_TIMESTAMP()," << temperatureCelsius;
		ssQuery << "," << humidity << "," << voltage << ")";
		std::cout << "query=\"" << ssQuery.str() << "\"\n";
		if (mysql_real_query(mysql, ssQuery.str().c_str(), (unsigned long)ssQuery.str().size()))
		{
			showError(mysql);
		}
		// erase cumulative buffer up util the end of the data //
		cumulativeBuffer = cumulativeBuffer.substr(dataEnd + 1);
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
