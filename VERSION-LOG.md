# Version Log
## 2.1.0
- **05d1130f**: readme – Update with new way of building, simpler.
- **be96a08f**: FTXUI – Add in and compile for PI.
- **39add567**: Move to 64 bit and compile openssl.
- **307d54bc**: fixup! BUMP 2.0.0.
- **475e85db**: README – Forgot to update README with the new GPIOs.
## 2.0.0
- **6bff81e**: schematic – Add in the schematic.
- **943b647**: setpoint – Drop the use of the rotary button.
- **f81f9cc**: TCA9548A – Drop the need of this.
- **867137a**: lcd – Drop some delay down.
- **98ccd92**: techtool – Update the way we poll sensor data.
- **c7135bc**: relay – Give the option to use high and low trigger relays.
- **62e067d**: alarm – Shorten up alarm loop.
- **d05c6aa**: tech-tool – Add a readme.
## 1.3.0
- **d364e4e**: log:  clean up duplicated work.
- **bfd3a4f**: tech-tool:  add in the ability to look at the service.
- **5e58dc0**: tech-tool:  clean up to be easier to read.
- **9e9c17a**: MIT License.
- **bd539c0**: openssl:  bring on the source to keep in the vendor folder.
- **eb1aa7e**: refrigeration:  don't start on install.

## 1.2.0
- **d0f1bbe**: Timer – On startup, displaying thousands of hours, then clears up.
- **61aad73**: Electric Heat – Ability to disable if not present.
- **5702ff7**: SSL – Write-up on how to create the files and what is needed.
- **6781e8a**: Client – Update to be more in line with rest of config descriptions.
- **87e5e9c**: Secure – Setup SSL certificate verification.
- **21fcab3**: Certs – Remove the define out of the code and put in config file.
- **19a9822**: Variables – Remove all seconds and go with minutes.

## 1.1.1
- Variables – Remove all seconds from the config file, use minutes for simplicity.

## 1.1.0
- Setpoint – Setup min/max setpoint in config file.
- Setpoint – Setup to use buttons or 10K rotary encoder.
- LCD – Drop sleep times to speed up the display.
- Hourmeter – Add to display when not in hotspot.
- Tech-tool – Move sensor collection to its own function.

## 1.0.5
- Hotspot – Add ability to turn on/off the hotspot.
- Display – Cleaned up code.
- Anti-cycle – Now display "AC" when anti-cycle is on.

## 1.0.4
- Tech-tool – Save-as-you-go quicker update.
- Timer – Track compressor run hours.
- Fans – Now able to leave the fan on/off or on all the time.
- Misc – Updates.

## 1.0.3
- Pretrip – Add a warning at the end to let you know it's completed and passed.
- Email – Removed some data not really needed, as in alarm state all relays are off.
- Alarm – On alarm, send the state to the server right then.
- Email – Fix up: only send the email once; also added more debugging.
- Sensor – If they fail (e.g., unplugged), return -375 and print out the error.

## 1.0.2
- Add Pretrip Mode.
- Display IP addresses.

## 1.0.1
- Add password to the database to save the hotspot password.
- Tech-tool – Allow reset to default.
- Only call Alarm function once in alarm.

## 1.0.0
- First release.