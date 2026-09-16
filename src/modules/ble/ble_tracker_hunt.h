#ifndef __BLE_TRACKER_HUNT_H__
#define __BLE_TRACKER_HUNT_H__

// Finds BLE item trackers in range -- AirTags and other Find My accessories,
// Samsung SmartTags, Tiles, Google Find My Device tags and the generic
// anti-lost tags sold as AirTag alternatives -- and then hunts a chosen one:
// the closer it gets, the faster and higher the device beeps.
void bleTrackerHunt();

#endif
