#include "PTACommandEventHandler.h"
#include <fstream>
#include <iostream>
#include <Windows.h>
#include <io.h>
#include "PTAInstanceHandler.h"
#include "IniFile.h"

PTACommandEventHandler::PTACommandEventHandler() : CommandEventHandler()
{
	_app = Application::get();
	_ui = _app->userInterface();

}

void PTACommandEventHandler::notify(const Ptr<CommandEventArgs> &eventArgs)
{
	std::string event = eventArgs->firingEvent()->name();
	_inputChangedHandler = &PTAInstanceHandler_.inputChangedHandler;

	if (event == "OnExecute")
	{
		Ptr<Command> cmd = eventArgs->command();
		Ptr<CommandInputs> cmdInputs = cmd->commandInputs();
		int progValue = 0;
		//	Ptr<ProgressDialog> progDialog = _ui->createProgressDialog();

		Ptr<StringValueCommandInput> ipInput = cmdInputs->itemById("ipInput");
		Ptr<StringValueCommandInput> portInput = cmdInputs->itemById("portInput");

		if (!ipInput || !portInput)
		{
			_ui->messageBox("Enter ip address or port");
			_inputChangedHandler->clearLists();
			return;
		}

		Ptr<Products> products = _app->activeDocument()->products();

		if (!products) {
			_inputChangedHandler->clearLists();
			return;
		}

		_cam = Application::get()->activeDocument()->products()->itemByProductType("CAMProductType");


		if (!_cam) {
			_ui->messageBox("No CAM object");
			return;
		}
		/*
		if (progDialog)
			progDialog->isBackgroundTranslucent(false);

		progDialog->show("Sending data to Axis", "Percentage %p, Current value: %v, Steps: %m", 0, 3, 1);
		*/
		// Write config

		Ptr<DropDownCommandInput> setupInput = cmdInputs->itemById("setupSelect");
		Ptr<DropDownCommandInput> opInput = cmdInputs->itemById("operationSelect");

		if (IniFile::isOk())
		{
			//	progDialog->message("Writing config values");
			IniFile::setString("Settings", "IP", ipInput->value());
			IniFile::setString("Settings", "PORT", portInput->value());
			//	progDialog->progressValue(++progValue);
		}

		if (!setupInput || !opInput)
		{
			_ui->messageBox("fail setupInput || opInput");
			_inputChangedHandler->clearLists();
			return;
		}

		if (!_inputChangedHandler->hasFile())
		{
			Ptr<ObjectCollection> opsToPost = ObjectCollection::create();

			Ptr<ListItem> setupSelected = setupInput->selectedItem();
			//	Ptr<ListItem> opSelected = opInput->selectedItem();

			if (!setupSelected)
			{
				_ui->messageBox("No setup selected");
				_inputChangedHandler->clearLists();
				return;
			}

			//progDialog->message("Gathering operations...");

			int itemCount = 0;
			for (Ptr<ListItem> listItem : opInput->listItems())
				if (listItem->isSelected()) itemCount++;

			if (itemCount > 0)
			{
				Ptr<Setup> setup = _cam->setups()->item(setupSelected->index());
				
				if (!setup) {
					_ui->messageBox("No setup selected or cant find the setup selected");
					return;
				}

				for(int i = 0; i < opInput->listItems()->count(); i++)
				{
					Ptr<ListItem> opSelected = opInput->listItems()->item(i);
					Ptr<OperationBase> op = _inputChangedHandler->_operationList[_app->activeDocument()->name()][setupSelected->index()][opSelected->index()];

					if (opSelected->isSelected())
					{
						std::string objectType = op->objectType();
						if(objectType == "adsk::cam::Operation" || objectType == "adsk::cam::CAMPattern")
							opsToPost->add(op);
						else
							continue;
					}
				}

				std::string willPost;

				for (int i = 0; i < opsToPost->count(); i++)
				{
					Ptr<OperationBase> op = opsToPost->item(i);
					
					if (std::string(op->objectType()) == "adsk::cam::Operation")
					{
						if (!generateToolpath(op, true))
						{
							_ui->messageBox("Toolpath for \"" + op->name() + "\" is not valid, exiting");
							return;
						}
					}
				}

				//			return;
							//progDialog->progressValue(++progValue);
			}
			else {
				for (Ptr<Setup> setup : _cam->setups())
				{
					if (setup->name() == setupSelected->name())
					{
						/*
						bool isValid = true;

						for (Ptr<Operation> op : setup->allOperations())
						{
							if (!op->hasToolpath()) {
								isValid = false;
								break;
							}
						}

						if (isValid)
							opsToPost->add(setup);
							*/
						for (Ptr<Operation> op : setup->allOperations())
						{
							if (!op->isToolpathValid())
								generateToolpath(op, true);

							opsToPost->add(op);
						}


					}
				}
				//progDialog->progressValue(++progValue);
			}
			//progDialog->message("Post processing...");
			std::string ngcFilename = postProcess(opsToPost);

			if (ngcFilename.empty())
			{
				_ui->messageBox("postProcess failed.. Exiting..");
				_inputChangedHandler->clearLists();
				return;
			}
			std::string wsFilename = "";
			//		Sleep(3000);
					//progDialog->progressValue(++progValue);

			Ptr<BoolValueCommandInput> genSetupSheet = cmdInputs->itemById("genSetupSheet");

			if (genSetupSheet->value())
			{
				wsFilename = generateWorksheet(opsToPost);
				_ui->messageBox("Generated Setup Sheet: " + wsFilename);
			}

			if (!ngcFilename.empty())
			{
				if (sendFile(ngcFilename, ipInput->value(), portInput->value(), wsFilename))
					_ui->messageBox("Send successfull");
				else
				{
					_ui->messageBox("Send failed");
					_inputChangedHandler->clearLists();
					return;
				}
			}
			else {
				_ui->messageBox("Post process failed..");
			}

		}
		else {
			if (sendFile(_inputChangedHandler->filePath(), ipInput->value(), portInput->value()))
				_ui->messageBox("Send successfull");
			else
				_ui->messageBox("Send failed");
		}
	}
	else if (event == "OnDestroy") {
		_inputChangedHandler->clearLists();
	}
	//progDialog->hide();
}

bool PTACommandEventHandler::sendFile(const std::string filePath, const std::string ipAddr ,const std::string port, const std::string workSheetPath)
{
	PTASocket socket;
	if (!socket.connectTo(ipAddr, port)) {
		_ui->messageBox("Failed to connect to server..");
		return false;
	}
//	_ui->messageBox("Connected to: " + ipAddr + ':' + port);

	std::string md5 = getMD5(filePath);

	if (md5.empty()) return false;

	size_t fileSize;
	char *fileData = getFileData(filePath, &fileSize);

	if (fileData == nullptr) return false;

	PTAPacket packet(PTA_PACKET_NGC, md5.c_str(), fileData, fileSize);

	delete[] fileData;
	if (socket.sendPacket(&packet) < 0)
	{
		_ui->messageBox("Failed to send packet");
		socket.disconnect();
		return false;
	}

	socket.disconnect();
	/*
	socket.connectTo(ipAddr, port);

	fileData = getFileData(workSheetPath, &fileSize);

	PTAPacket ssPacket(PTA_PACKET_SS, getMD5(workSheetPath).c_str(), fileData, fileSize);
	socket.sendPacket(&ssPacket);
*/

	return true;
}

#include <stdio.h>
std::string PTACommandEventHandler::postProcess(Ptr<ObjectCollection> opsToPost)
{
	char tmpFilename[1024];
	int errno = tmpnam_s((char *)&tmpFilename, 128);
	std::string tmpFilePath(tmpFilename);

	std::string outputPath = tmpFilePath.substr(0, tmpFilePath.find_last_of("\\/"));
	std::string outputFilename = tmpFilePath.substr(tmpFilePath.find_last_of("\\/") + 1);

	std::string postConfig = _cam->genericPostFolder() + '/' + "linuxcnc.cps";

	PostOutputUnitOptions postUnits = PostOutputUnitOptions::MillimetersOutput;

	Ptr<PostProcessInput> postInput = PostProcessInput::create(outputFilename, postConfig, outputPath, postUnits);

	if (postInput == nullptr)
		_ui->messageBox("Failed to create post processor input");

	postInput->isOpenInEditor(false);


	if (!_cam->postProcess(opsToPost, postInput))
	{
		std::string errMsg;
		int errorCode =_app->getLastError(&errMsg);
		_ui->messageBox("Failed to post:\n" + errMsg + "(" + std::to_string(errorCode) + ")");
		return std::string();
	}

//	_ui->messageBox("post done");

//	_cam->generateSetupSheet(opsToPost, SetupSheetFormats::HTMLFormat, outputPath);
	_ui->messageBox(outputPath + "\\" + outputFilename + ".ngc");
	return outputPath + "\\" + outputFilename + ".ngc";
}

std::string PTACommandEventHandler::generateWorksheet(Ptr<ObjectCollection> opsToPost)
{
	char tmpFilename[1024];
	int errno = tmpnam_s((char *)&tmpFilename, 128);
	std::string tmpFilePath(tmpFilename);

	_cam->generateSetupSheet(opsToPost, SetupSheetFormats::HTMLFormat, tmpFilePath, false);

	return tmpFilePath + "\\" + _app->activeDocument()->name() + ".html";
}


bool PTACommandEventHandler::generateToolpath(Ptr<Operation> op, bool askConfirmation)
{
	if (!op->isValid()) return false;

	if (op->operationState() == OperationStates::IsValidOperationState)
		return true;

	if (op->operationState() == OperationStates::IsInvalidOperationState) {
		if (askConfirmation) {
			if (_ui->messageBox("Toolpath for " + op->name() + " is invalid!\nDo you want to generate the toolpath?", "Generate Toolpath", YesNoButtonType) == DialogNo)
				return false;
		}

			Ptr<ProgressDialog> genProg = _ui->createProgressDialog();
			genProg->show("Generating Toolpath", "Generating Toolpath", 0, 100);

			_cam->generateToolpath(op);


			while (op->isGenerating()) {
				std::string progress = op->generatingProgress();
				int prog = std::stoi(progress.substr(0, progress.find_last_of(".")));
				genProg->progressValue(prog);
				Sleep(10);
			}

			genProg->hide();

			if (op->operationState() == OperationStates::IsValidOperationState)
				return true;
		
	}

	return false;
}

char *PTACommandEventHandler::getFileData(const std::string filePath, size_t *size)
{
	char *outputBuffer = nullptr;
	std::ifstream fileToSend(filePath, std::ifstream::binary | std::ifstream::ate);

	if (fileToSend.is_open())
	{
		*size = fileToSend.tellg();
		fileToSend.seekg(fileToSend.beg);

		outputBuffer = new char[*size];

		fileToSend.read(outputBuffer, *size);
		fileToSend.close();
	}

	return outputBuffer;
}

#include <algorithm>
std::string PTACommandEventHandler::getMD5(const std::string filePath)
{
	int tries = 5;
	try {
		while (tries > 0)
		{
			std::ifstream file(filePath.c_str(), std::ifstream::binary | std::ifstream::ate);

			if (file) {
				std::streamoff fileSize = file.tellg();
				file.seekg(file.beg);

				char *buffer = new char[fileSize];
				file.read(buffer, fileSize);


				std::string md5string = md5(std::string(buffer, fileSize));
				file.close();
				delete[] buffer;
				return md5string;
			}

			Sleep(500);
		}
	}
	catch (const std::ifstream::failure &e)
	{
		_ui->messageBox("Fail: " + std::string(e.what()));
	}
	
	return "";
}