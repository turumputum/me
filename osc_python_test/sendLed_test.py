import time
from pythonosc.dispatcher import Dispatcher
from pythonosc import udp_client
from pythonosc import osc_server


client = udp_client.SimpleUDPClient("192.168.0.1", 7000)

def button_0_rep(unused_addr, args, val):
    if(val==1):
        client.send_message("/custom_topic:/composition/layers/3/clips/1/connect", 1)
        print("Button_0 report")

def button_1_rep(unused_addr, args, val):
    if(val==1):
        client.send_message("/custom_topic:/composition/layers/3/clips/2/connect", 1)
        print("Button_1 report")

def button_2_rep(unused_addr, args, val):
    if(val==1):
        client.send_message("/custom_topic:/composition/layers/3/clips/3/connect", 1)
        print("Button_2 report")

def button_3_rep(unused_addr, args, val):
    if(val==1):
        client.send_message("/custom_topic:/composition/layers/3/clips/4/connect", 1)
        print("Button_3 report")

dispatcher = Dispatcher()
dispatcher.map("/dev_1/button_0", button_0_rep, "val")
dispatcher.map("/dev_1/button_1", button_1_rep, "val")
dispatcher.map("/dev_1/button_2", button_2_rep, "val")
dispatcher.map("/dev_1/button_3", button_3_rep, "val")

server = osc_server.ThreadingOSCUDPServer(("192.168.0.1", 8000), dispatcher)
print("Serving on {}".format(server.server_address))
server.serve_forever()

while(1):
    time.sleep(1)




#custom_topic:/composition/layers/3/clips/2/connect