import hid
import sys
import time

VENDOR_ID = 0x303A
PRODUCT_ID = 0x1324
REPORT_ID = 0x03

shakespeare = (
    b"to be, or not to be: that is the question: whether 'tis nobler in the mind to suffer the slings and arrows of outrageous fortune, or to take arms against a sea of troubles, and by opposing end them? to die: to sleep; no more; and by a sleep to say we end the heart-ache and the thousand natural shocks that flesh is heir to, 'tis a consummation devoutly to be wish'd. to die, to sleep; to sleep: perchance to dream: ay, there's the rub; for in that sleep of death what dreams may come when we have shuffled off this mortal coil, must give us pause: there's the respect that makes calamity of so long life. for who would bear the whips and scorns of time, th' oppressor's wrong, the proud man's contumely, the pangs of despised love, the laws delay, the insolence of office and the spurns that patient merit of th' unworthy takes, when he himself might his quietus make with a bare bodkin? who would fardels bear, to grunt and sweat under a weary life, but that the dread of something after death, the undiscover'd country from whose bourn no traveller returns, puzzles the will and makes us rather bear those ills we have than fly to others that we know not of? thus conscience does make cowards of us all; and thus the native hue of resolution is sicklied o'er with the pale cast of thought, and enterprises of great pitch and moment with this regard their currents turn awry, and lose the name of action."
)

def crc8(data: bytes, poly=0x07, init=0x00) -> bytes:
    crc = init
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc

def find_device():
    print("Enumerating HID devices:")
    comms_path = None
    for d in hid.enumerate():
        if d['vendor_id'] == VENDOR_ID and d['product_id'] == PRODUCT_ID:
            print(f"  path={d['path']} interface_number={d.get('interface_number')} usage_page={d.get('usage_page')} usage={d.get('usage')}")
            if d.get('interface_number', 0) == 1:
                comms_path = d['path']
    return comms_path

def main():
    path = find_device()
    if not path:
        print("Device not found.")
        sys.exit(1)

    dev = hid.device()
    dev.open_path(path)
    print(f"Opened device: {path}")

    # pattern = b"The quick brown fox jumps over the lazy dog. $i$/20000\n"
    # repeat_count = (20000 // len(pattern)) + 1
    # test_data = (pattern * repeat_count)[:20000]

    #print(f"Shakespearean length: {len(test_data)}")

    first = 0x80
    mid   = 0x40
    last  = 0x20
    
    total_len = 2300
    packet_size = 43
    num_packets = (total_len + packet_size - 1) // packet_size  # ceil division

    print("Total packets to send: ", num_packets)

    for i in range(num_packets):
        # start = i * packet_size
        # end = start + packet_size
        # stripped_data = bytes([str(test_data[start:end]).replace("$i$", i)])
        
        text_to_send = f"The quick brown fox jumps over the {i}".encode()
        stripped_data = text_to_send + b".\n"
        stripped_data_len = len(stripped_data) & 0xFF

        flag = 0x00
        if (i == num_packets - 1):
            flag = last
        elif (i == 0):
            flag = first
        else:
            flag = mid

        rem = num_packets - i - 1
        rem_high = (rem >> 8) & 0xFF
        rem_low = rem & 0xFF

        out_payload = bytes([flag, rem_low, rem_high, stripped_data_len]) + stripped_data
        out_payload = out_payload + bytes([crc8(out_payload)])
        out_payload = bytes([REPORT_ID]) + out_payload

        print(f"Remaining: {rem} Writing {len(out_payload)} bytes: {out_payload.hex()}")
        dev.write(out_payload)

    dev.close()

if __name__ == "__main__":
    main()
