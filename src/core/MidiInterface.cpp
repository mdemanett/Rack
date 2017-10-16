#include <list>
#include <algorithm>
#include "rtmidi/RtMidi.h"
#include "core.hpp"
#include "MidiInterface.hpp"

using namespace rack;



/**
 * MidiIO implements the shared functionality of all midi modules, namely:
 * + Channel Selection (including helper for storing json)
 * + Interface Selection (including helper for storing json)
 * + rtMidi initialisation (input or output)
 */
MidiIO::MidiIO(bool isOut) {
	channel = -1;
	this->isOut = isOut;

	if (isOut) {
		fprintf(stderr, "Midi Out is currently not supported (will be added soon)");
	}
};

void MidiIO::setChannel(int channel) {
	this->channel = channel;
}

std::unordered_map<std::string, MidiInWrapper*> MidiIO::midiInMap = {};

json_t *MidiIO::addBaseJson(json_t *rootJ) {
	if (deviceName != "") {
		json_object_set_new(rootJ, "interfaceName", json_string(deviceName.c_str()));
		json_object_set_new(rootJ, "channel", json_integer(channel));
	}
	return rootJ;
}

void MidiIO::baseFromJson(json_t *rootJ) {
	json_t *portNameJ = json_object_get(rootJ, "interfaceName");
	if (portNameJ) {
		openDevice(json_string_value(portNameJ));
	}

	json_t *channelJ = json_object_get(rootJ, "channel");
	if (channelJ) {
		setChannel(json_integer_value(channelJ));
	}
}

std::vector<std::string> MidiIO::getDevices() {
	/* Note: we could also use an existing interface if one exists */
	static RtMidiIn * m = new RtMidiIn();

	std::vector<std::string> names = {};

	for (int i = 0; i < m->getPortCount(); i++) {
		names.push_back(m->getPortName(i));
	}

	return names;
}

void MidiIO::openDevice(std::string deviceName) {

	MidiInWrapper *mw = midiInMap[deviceName];

	if (this->id > 0 || this->deviceName != "") {
		close();
	}

	if (!mw) {
		try {
			mw = new MidiInWrapper();
			midiInMap[deviceName] = mw;


			for (int i = 0; i < mw->getPortCount(); i++) {
				if (deviceName == mw->getPortName(i)) {
					mw->openPort(i);
					break;
				}
			}
		}
		catch (RtMidiError &error) {
			fprintf(stderr, "Failed to create RtMidiIn: %s\n", error.getMessage().c_str());
			return;
		}
	}

	this->deviceName = deviceName;

	/* TODO: this works for now, but is not ideal. If a clock is added and connected to a module
	 * the time messages will still be received after the clock is removed. This adds an overhead
	 * which can be avoided but I want to find a good solution.*/
	if (!ignore_midiTime || !ignore_midiTime || !ignore_midiSense){
		midiInMap[deviceName]->ignoreTypes(ignore_midiSysex, ignore_midiTime, ignore_midiSense);
	}

	id = midiInMap[deviceName]->add();
}

std::string MidiIO::getDeviceName() {
	return deviceName;
}

double MidiIO::getMessage(std::vector<unsigned char> *msg) {
	std::vector<unsigned char> next_msg;

	MidiInWrapper *mw = midiInMap[deviceName];

	if (!mw) {
		fprintf(stderr, "Device not opened!: %s\n", deviceName.c_str());
		return 0;
	}

	double stamp = midiInMap[deviceName]->getMessage(&next_msg);

	if (next_msg.size() > 0) {
		for (auto kv : mw->idMessagesMap) {
			mw->idMessagesMap[kv.first].push_back(next_msg);
			mw->idStampsMap[kv.first].push_back(stamp);
		}
	}

	if (mw->idMessagesMap[id].size() <= 0) {
		*msg = next_msg;
		return stamp;
	}

	*msg = mw->idMessagesMap[id].front();
	stamp = mw->idStampsMap[id].front();
	mw->idMessagesMap[id].pop_front();
	return stamp;
}

bool MidiIO::isPortOpen() {
	return id > 0;
}

void MidiIO::close() {

	MidiInWrapper * mw = midiInMap[deviceName];

	if (!mw || id < 0) {
		//fprintf(stderr, "Trying to close already closed device!\n");
		return;
	}

	mw->erase(id);

	if (mw->subscribers == 0) {
		mw->closePort();
		midiInMap.erase(deviceName);
	}

	id = -1;
	deviceName = "";
}


void MidiItem::onAction() {
	midiModule->resetMidi(); // reset Midi values
	midiModule->openDevice(text);
}

void MidiChoice::onAction() {
	Menu *menu = gScene->createMenu();
	menu->box.pos = getAbsolutePos().plus(Vec(0, box.size.y));
	menu->box.size.x = box.size.x;

	{
		MidiItem *midiItem = new MidiItem();
		midiItem->midiModule = midiModule;
		midiItem->text = "";
		menu->pushChild(midiItem);
	}

	std::vector<std::string> deviceNames = midiModule->getDevices();
	for (int i = 0; i < deviceNames.size(); i++) {
		MidiItem *midiItem = new MidiItem();
		midiItem->midiModule = midiModule;
		midiItem->text = deviceNames[i];
		menu->pushChild(midiItem);
	}
}

void MidiChoice::step() {
	if (midiModule->getDeviceName() == "") {
		text = "No Device";
		return;
	}
	std::string name = midiModule->getDeviceName();
	text = ellipsize(name, 15);
}

void ChannelItem::onAction() {
	midiModule->resetMidi(); // reset Midi values
	midiModule->setChannel(channel);
}

void ChannelChoice::onAction() {
	Menu *menu = gScene->createMenu();
	menu->box.pos = getAbsolutePos().plus(Vec(0, box.size.y));
	menu->box.size.x = box.size.x;

	{
		ChannelItem *channelItem = new ChannelItem();
		channelItem->midiModule = midiModule;
		channelItem->channel = -1;
		channelItem->text = "All";
		menu->pushChild(channelItem);
	}
	for (int channel = 0; channel < 16; channel++) {
		ChannelItem *channelItem = new ChannelItem();
		channelItem->midiModule = midiModule;
		channelItem->channel = channel;
		channelItem->text = stringf("%d", channel + 1);
		menu->pushChild(channelItem);
	}
}

void ChannelChoice::step() {
	text = (midiModule->channel >= 0) ? stringf("%d", midiModule->channel + 1) : "All";
}