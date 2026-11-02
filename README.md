# ReflectorMic

A small D-Star voice terminal with a microphone, a speaker, and push-to-talk. ReflectorMic connects to internet reflectors over Wi-Fi and uses the MBELIB-neo R13 software codec to send and receive voice. No RF hardware or separate radio is required.

<a href="https://docs.m5stack.com/en/core/Atom_VoiceS3R"><img src="https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1182/Atom_EchoS3R_01.webp" alt="M5Stack Atom VoiceS3R with its built-in microphone, speaker and PTT button" width="440"></a>

*M5Stack Atom VoiceS3R. Product photo hosted by [M5Stack](https://docs.m5stack.com/en/core/Atom_VoiceS3R).*

**Only the M5Stack Atom VoiceS3R has been tested.** It is USB powered and uses its built-in microphone, speaker, and front button. Other Atom models are not currently supported.

ReflectorMic supports **REF (D-Plus), XRF (DExtra), DCS, and XLX through its DCS interface**. Reflector selection and settings are controlled through a local web interface. The speaker is predictably terrible because of its size. Voice is best effort; occasional audio gaps are possible. Other stations say it sounds great however.

## First-time setup

With ReflectorMic firmware installed:

1. Connect USB power. A short beep plays after audio initialization.
2. On your phone or computer, join the **ReflectorMic-XXXXXX** Wi-Fi network. Its setup password is **`reflectormic`**.
3. Open the setup page. If it does not open automatically, visit **[192.168.4.1](http://192.168.4.1/)**.
4. Expand **Setup & Diagnostics**, then find **Wi-Fi setup**. Select **Scan networks**, choose your **2.4 GHz** network, and enter its password. Select **Save and reboot**.
5. Rejoin your usual Wi-Fi network and open **[reflectormic.local](http://reflectormic.local/)**. If that address does not work, use the address assigned to ReflectorMic by your router.
6. Under **Station and audio**, enter your licensed amateur-radio callsign and choose your station settings. Select **Save settings**. Use your own callsign and follow the reflector's access requirements.
7. Upload the Pi-Star directory using the steps below, then select a reflector and module under **Reflector control** and press **Link**.

The web interface is intended for a trusted local network and has no login. Keep it off the public internet.

## Upload the Pi-Star D-Star directory

**A directory upload is required before linking.** ReflectorMic uses this file to look up reflector addresses. It does not download the directory from the internet itself.

1. Visit **[Pi-Star downloads](https://www.pistar.uk/downloads/)** on your computer or phone.
2. Download **`DStar_Hosts.json`**. Save the JSON file itself, rather than the downloads web page or a Pi-Star operating-system image.
3. Open the ReflectorMic web interface and expand **Setup & Diagnostics**.
4. Find **Update DStar_Hosts.json**, choose the downloaded file, and press **Upload directory**.
5. Wait for the upload to finish and the directory status to confirm that the file is installed.
6. Under **Reflector control**, enter or search for a reflector, choose its module, and press **Link**. Wait for **Live status** to show the connection before transmitting.

The upload's filename does not matter. ReflectorMic checks the contents and stores a valid file as **`DStar_Hosts.json`** in LittleFS. The directory survives a restart. Invalid uploads are rejected before replacing the installed directory; the maximum file size is **1,250 KiB**.

Repeat these steps with a fresh download when you want to update reflector addresses. Upload the file as downloaded, without editing its format.

- **Directory unavailable or file missing:** upload the directory first.
- **Reflector not found:** check the reflector name and install a current directory.
- **Upload rejected:** check that the selected file is the Pi-Star D-Star JSON directory and is within the size limit.

## Talking and listening

- **Hold the front PTT button to talk; release it to listen.** ReflectorMic receives while it is not transmitting.
- Transmission ends automatically at the configured **PTT timeout**, which defaults to **180 seconds**. After a timeout, release PTT before starting again.
- Adjust the **Speaker gain** and **Microphone gain** sliders in **Station and audio**, then press **Save settings**.
- Use **Reflector control** to change reflector or module. **Unlink** disconnects the current reflector.
- Choose **Last successful**, **Configured default**, or **Manual only** under **Power-up linking** to control startup behavior.

ReflectorMic retries Wi-Fi and reflector connections after interruptions. If Wi-Fi is unavailable, the setup access point returns while connection attempts continue.

The first active voice stream takes priority. Fast data, images, DPRS, and text are ignored so they do not interrupt voice playback.

## Local echo test

Open **Setup & Diagnostics → Local echo test** and select **Arm echo test**.

Arm the test, then hold PTT to record. Release PTT to finish encoding and hear the recording through the speaker.

Recordings are limited to **10 seconds**. Wait for processing and playback to finish before starting another test. The echo test uses the voice codec locally and does not transmit the recording to a reflector.

**Play speaker test tone** checks speaker output independently of the microphone.

## Basic troubleshooting

**Live status** shows the connection state, firmware build, and codec version. Under **Setup & Diagnostics → Receive troubleshooting**:

- **Restart receive audio** restarts playback while keeping the reflector linked.
- **Save RX diagnostic**, when available, saves a short receive capture for troubleshooting. Captures can contain received speech and station information; review them before sharing.

For missing audio, check the link status and gain settings, then try the local echo test.

## License

ReflectorMic is licensed under **[GNU GPL version 3 or later](LICENSE)**. You may use, modify, and redistribute it. Distributed derivative works must remain under the GPL and provide their corresponding source as the license requires.

Third-party code retains its original notices and licenses; see **[NOTICE.md](NOTICE.md)**. The linked M5Stack product photo and trademarks are not covered by the project's software license.
