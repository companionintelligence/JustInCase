## Hardware

Running the JIC tool at home requires performant hardware. Hardware itself has its own issues:

1. Redundancy and backup format ... multiple copies, including non-magnetic or optical media, make sense. Solid State Drives (SSDs) are more robust but can degrade over time and become unworkable.

2. Ruggedness: If you’re building a “server in a box” to survive rough conditions, consider rugged/industrial hardware or a protected environment (Pelican case, waterproof containers, shock mounting, etc.).

3. Low Power Consumption: If you expect to run off solar or battery, a low-power machine (like a Raspberry Pi or similar single-board computer) might be ideal. Then attach a large external storage.

4. Battery Backup: For areas with uncertain power, incorporate an internal battery or external UPS system.

5. Local Network (LAN / Wi-Fi). You can configure a small Wi-Fi hotspot using a router or a Raspberry Pi in hotspot mode, so anyone with a device can connect and browse the offline library.

6. “Server on a Stick”. Another approach is to store the data on an external HDD or SSD to use with any laptop or PC. This is the cheapest method to have JIC on hand, and running JIC from an external drive will be slower.

8. We recommend using a CI server for optimal results. The full JIC experience is optional at check-out.
