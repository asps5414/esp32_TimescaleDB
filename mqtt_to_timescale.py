import os
import json
import psycopg2
import paho.mqtt.client as mqtt

conn = psycopg2.connect(
    host=os.environ.get("PG_HOST", "localhost"),
    database=os.environ.get("PG_DATABASE", "postgres"),
    user=os.environ.get("PG_USER", "postgres"),
    password=os.environ.get("PG_PASSWORD", ""),  # 密碼用環境變數設定，不寫在程式裡
)

def on_message(client, userdata, msg):

    data = json.loads(msg.payload)

    device = data["device"]
    temp = data["temp"]

    cur = conn.cursor()

    cur.execute(
        "INSERT INTO sensor_data(time, device_id, temperature) VALUES (NOW(), %s, %s)",
        (device, temp)
    )

    conn.commit()
    cur.close()

    print("Inserted:", device, temp)

client = mqtt.Client()

client.connect("localhost",1883)

client.subscribe("factory/sensor")

client.on_message = on_message

client.loop_forever()