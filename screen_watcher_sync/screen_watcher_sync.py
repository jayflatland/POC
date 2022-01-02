import numpy as np
from mss import mss
from PIL import Image


"""
This function
1) watches a specific rectangle on the screen
2) waits until it sees a lot of red
3) then returns

May need to
 - pip install numpy
 - pip install mss
 - pip install Pillow
"""

def wait_for_level_start():
    with mss() as sct:
        while True:
            screenShot = sct.grab({'left': 500, 'top': 500, 'width': 100, 'height': 100})
            img = Image.frombytes('RGB', (screenShot.width, screenShot.height), screenShot.rgb)
            a = np.array(img)
            r_avg = a[:,:,0].mean()
            g_avg = a[:,:,1].mean()
            b_avg = a[:,:,2].mean()
            print(r_avg, g_avg, b_avg)
            if r_avg > 180 and g_avg < 50 and b_avg < 50:
                break

print("Waiting for start...")
wait_for_level_start()
print("Started!")