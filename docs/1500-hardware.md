## Hardware

Running the JIC tool at home requires performant hardware. Hardware itself has its own issues:

1. Redundancy and backup format ... multiple copies, including non-magnetic or optical media, make sense. Solid State Drives (SSDs) are more robust but can degrade over time and become unworkable.

2. Ruggedness: If you’re building a “server in a box” to survive rough conditions, consider rugged/industrial hardware or a protected environment (Pelican case, waterproof containers, shock mounting, etc.).

3. Low Power Consumption: If you expect to run off solar or battery, a low-power machine (like a Raspberry Pi or similar single-board computer) might be ideal. Then attach a large external storage.

4. Battery Backup: For areas with uncertain power, incorporate an internal battery or external UPS system.

5. Local Network (LAN / Wi-Fi). You can configure a small Wi-Fi hotspot using a router or a Raspberry Pi in hotspot mode, so anyone with a device can connect and browse the offline library.

6. “Server on a Stick”. Another approach is to store the data on an external HDD or SSD to use with any laptop or PC. This is the cheapest method to have JIC on hand, and running JIC from an external drive will be slower.

7. Local backups: Keep multiple copies of the dataset and models in separate locations.

## Storage capacity

Choose the drive by the content you want to carry. The system itself is small; the library is what takes the space. The figures below are the download sizes of the helper profiles in the [README](../README.md#3-load-the-library) and the local models. The search index is extra and is not included, so leave headroom.

| Drive | What fits |
|---|---|
| 256 GB | The core library, the emergency ZIMs (about 20 GB) and the local models, with room to grow |
| 512 GB | Everything above plus a full encyclopedic ZIM set (about 240 GB), with space to spare |
| 1 TB | The full library plus room for your own files, and a second copy of the dataset |

## Storage medium

- **SSD (external or internal): recommended for regular use.** Faster to search, and sturdier than a spinning disk.
- **USB flash drive: good for transport and a reserve copy.** Quality and write endurance vary a lot between models, so do not rely on one for running the library every day.
- **Mechanical hard drive: good value for large collections.** It is sensitive to knocks, so keep it in a protected case when stored or moved.

## Before you store it

Check the setup works before the drive goes into storage:

1. Start the stack with `docker compose up -d` and check with `docker compose ps` that the services are running.
2. Open the web UI from another device on the same network.
3. Ask one question and check that the answer cites a document from the library.
4. Stop the stack with `docker compose down`. Do not add `-v`: that deletes the library and index volumes.

Start it again every few months. Check that it still answers, refresh the content with the helper scripts, and keep the backup copies in step with the working copy.
