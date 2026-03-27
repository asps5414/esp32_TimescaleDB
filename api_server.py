from flask import Flask, request, jsonify, render_template
import psycopg2

app = Flask(__name__)

conn = psycopg2.connect(
    host="localhost",
    database="postgres",
    user="postgres",
    password="1234"
)

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/data", methods=["GET"])
def get_data():

    cur = conn.cursor()

    cur.execute("""
        SELECT time, temperature
        FROM sensor_data
        ORDER BY time DESC
        LIMIT 50
    """)

    rows = cur.fetchall()

    data = []
    for row in rows:
        data.append({
            "time": row[0].strftime("%H:%M:%S"),
            "temp": row[1]
        })

    cur.close()

    response = jsonify(data[::-1])
    response.headers["Cache-Control"] = "no-cache"
    return response

@app.route("/sensor", methods=["POST"])
def receive_data():

    data = request.json

    device = data.get("device", "unknown")
    temp = data.get("temp", 0)

    cur = conn.cursor()

    cur.execute(
        """
        INSERT INTO sensor_data(time, device_id, temperature)
        VALUES (NOW(), %s, %s)
        """,
        (device, temp)
    )

    conn.commit()
    cur.close()

    print("Inserted:", device, temp)

    return jsonify({"status": "ok"})

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)