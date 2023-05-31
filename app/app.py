import paho.mqtt.client as mqtt
from flask import Flask, render_template, request, redirect, url_for
from flask_mongoengine import MongoEngine
import os
from google.cloud import bigquery
from google.oauth2 import service_account
from telebot import TeleBot
import threading
import json
from datetime import datetime

TOPIC_NFC = "solicitud/NFC"
TOPIC_ABRIR = "cmd/abrir"
TOPIC_TIMBRE = "solicitud/timbre"
TOPIC_ALARMA = "alarma"
TOPIC_MAX_INTENTOS = "maxIntentos"
TOPIC_CONF = "cmd/conf"

TOPICS_SUB = [TOPIC_NFC, TOPIC_TIMBRE, TOPIC_ALARMA, TOPIC_MAX_INTENTOS]

notify_when_open = True
CANAL_ID = -1001877074768

def on_connect(client, userdata, flags, rc):
    print("Connected with result code " + str(rc))
    for topic in TOPICS_SUB:
        client.subscribe(topic)


def on_message(client, userdata, msg):
    if msg.topic == TOPIC_NFC:
        data = msg.payload.decode("utf-8")
        data = json.loads(data)

        valido = False
        user = Usuario.objects(tag=data["tag"]).first()
        if user is None:
            client.publish(TOPIC_ABRIR, json.dumps({"OK": False}))
        else:
            client.publish(TOPIC_ABRIR, json.dumps({"OK": True}))
            valido = True
            if notify_when_open:
                bot.send_message(
                    chat_id=CANAL_ID,
                    text="{} acaba de abrir la puerta".format(user.nombre),
                )

        query = "INSERT INTO `puerta.tarjetas` (timestamp, tag, valido) VALUES ('{}', '{}', {})".format(
            datetime.now(), data["tag"], valido
        )
        bigquery_client.query(query)

    elif msg.topic == TOPIC_TIMBRE:
        bot.send_message(
            chat_id=CANAL_ID, text="¡Atento!\nAlguien ha tocado el timbre"
        )

    elif msg.topic == TOPIC_ALARMA:
        bot.send_message(
            chat_id=CANAL_ID,
            text="¡Atento!\nSe ha detectado presencia en la puerta",
        )
        query = "INSERT INTO `puerta.alarmas` (timestamp) VALUES ('{}')".format(
            datetime.now()
        )
        bigquery_client.query(query)

    elif msg.topic == TOPIC_MAX_INTENTOS:
        bot.send_message(
            chat_id=CANAL_ID,
            text="¡Atento!\nAlguien ha intentado abrir la puerta sin éxito el máximo de veces permitido, se ha bloqueado la lectura de NFC",
        )

        conf_actual = Configuracion.objects().first()
        conf_actual.isReadModeEnabled = False
        conf_actual.save()
        client.publish(TOPIC_CONF, json.dumps({
            'alarma': conf_actual.alarma,
            'tiempoOpen': conf_actual.tiempoOpen,
            'cardReadInterval': conf_actual.cardReadInterval,
            'isReadModeEnabled': conf_actual.isReadModeEnabled,
            'maxAttempts': conf_actual.maxAttempts
        }))


app = Flask(__name__)

#app.config["MONGODB_HOST"] = os.environ.get("MONGODB_HOST", "localhost")
app.config["MONGODB_HOST"] = os.environ.get('MONGODB_HOST', 'mongodb')
app.config["MONGODB_PORT"] = int(os.environ.get("MONGODB_PORT", 27017))
app.config["MONGODB_DB"] = os.environ.get("MONGODB_DB", "iot")
app.config["MONGODB_USERNAME"] = os.environ.get("MONGODB_USERNAME", "iotProyecto")
app.config["MONGODB_PASSWORD"] = os.environ.get("MONGODB_PASSWORD", "iotProyecto")

db = MongoEngine()
db.init_app(app)


class Usuario(db.Document):
    nombre = db.StringField()
    tag = db.StringField()


class Configuracion(db.Document):
    alarma = db.BooleanField()
    tiempoOpen = db.IntField()
    cardReadInterval = db.IntField()
    isReadModeEnabled = db.BooleanField()
    maxAttempts = db.IntField()


bot = TeleBot("6042518163:AAEPOBEa4fDBECZaeWHcqz367J660mpJr3Y")
client = mqtt.Client()

credentials = service_account.Credentials.from_service_account_file(
    "master-iot-381510-3c8ec272c4a4.json"
)
bigquery_client = bigquery.Client(credentials=credentials, project="master-iot-381510")


@bot.message_handler(commands=["start", "help"])
def send_welcome(message):
    bot.reply_to(
        message,
        "Hola, "
        + message.from_user.first_name
        + "\n\nEstos son mis comandos disponibles:\n\n/abrir - Abre la puerta\n/usuarios - Muestra los usuarios registrados\n/alarma - Activa o desactiva la alarma\n/tiempo - Cambia el tiempo de apertura de la puerta\n/intervalo - Cambia el intervalo de lectura de tarjetas\n/leer - Activa o desactiva el modo lectura\n/intentos - Cambia el número máximo de intentos de apertura de la puerta\n/configuracion - Muestra la configuración actual\n/notificar - Activa o desactiva las notificaciones de apertura de la puerta",
    )


@bot.message_handler(commands=["abrir"])
def telegram_abrir(message):
    client.publish(TOPIC_ABRIR, json.dumps({"OK": True}))
    bot.reply_to(message, "Se ha abierto la puerta")
    query = (
        "INSERT INTO `puerta.aperturas` (timestamp,origen) VALUES ('{}','{}')".format(
            datetime.now(), "telegram"
        )
    )

    if notify_when_open:
        bot.send_message(
            chat_id=CANAL_ID, text="Se ha abierto la puerta desde Telegram"
        )

    bigquery_client.query(query)


@bot.message_handler(commands=["usuarios"])
def telegram_usuarios(message):
    users = Usuario.objects()
    text = "Estos son los usuarios registrados:\n"
    for user in users:
        text += "{} - {}\n".format(user.nombre, user.tag)
    bot.reply_to(message, text)

@bot.message_handler(commands=["configuracion"])
def telegram_configuracion(message):
    config = Configuracion.objects().first()
    bot.reply_to(message, "Esta es la configuracion actual:\nAlarma: {}\nTiempo de apertura: {}\nIntervalo de lectura: {}\nModo lectura: {}\nMáximo de intentos: {}".format(config.alarma, config.tiempoOpen, config.cardReadInterval, config.isReadModeEnabled, config.maxAttempts))


@bot.message_handler(commands=["alarma"])
def telegram_alarma(message):
    config = Configuracion.objects().first()
    if config.alarma:
        config.alarma = False
        config.save()
        bot.reply_to(message, "Se ha desactivado la alarma")
    else:
        config.alarma = True
        config.save()
        bot.reply_to(message, "Se ha activado la alarma")

    client.publish(
        TOPIC_CONF,
        json.dumps(
            {
                "alarma": config.alarma,
                "tiempoOpen": config.tiempoOpen,
                "cardReadInterval": config.cardReadInterval,
                "isReadModeEnabled": config.isReadModeEnabled,
                "maxAttempts": config.maxAttempts,
            }
        ),
    )

@bot.message_handler(commands=["tiempo"])
def telegram_tiempo(message):
    config = Configuracion.objects().first()
    try:
        config.tiempoOpen = int(message.text.split(" ")[1])
        config.save()
        bot.reply_to(message, "Se ha cambiado el tiempo de apertura a {}".format(config.tiempoOpen))
        client.publish(
            TOPIC_CONF,
            json.dumps(
                {
                    "alarma": config.alarma,
                    "tiempoOpen": config.tiempoOpen,
                    "cardReadInterval": config.cardReadInterval,
                    "isReadModeEnabled": config.isReadModeEnabled,
                    "maxAttempts": config.maxAttempts,
                }
            ),
        )
    except:
        bot.reply_to(message, "Error, el comando es /tiempo <tiempo>")

@bot.message_handler(commands=["intervalo"])
def telegram_intervalo(message):
    config = Configuracion.objects().first()
    try:
        config.cardReadInterval = int(message.text.split(" ")[1])
        config.save()
        bot.reply_to(message, "Se ha cambiado el intervalo de lectura a {}".format(config.cardReadInterval))
        client.publish(
            TOPIC_CONF,
            json.dumps(
                {
                    "alarma": config.alarma,
                    "tiempoOpen": config.tiempoOpen,
                    "cardReadInterval": config.cardReadInterval,
                    "isReadModeEnabled": config.isReadModeEnabled,
                    "maxAttempts": config.maxAttempts,
                }
            ),
        )
    except:
        bot.reply_to(message, "Error, el comando es /intervalo <intervalo>")

@bot.message_handler(commands=["leer"])
def telegram_leer(message):
    config = Configuracion.objects().first()
    if config.isReadModeEnabled:
        config.isReadModeEnabled = False
        config.save()
        bot.reply_to(message, "Se ha desactivado el modo lectura")
    else:
        config.isReadModeEnabled = True
        config.save()
        bot.reply_to(message, "Se ha activado el modo lectura")
    client.publish(
        TOPIC_CONF,
        json.dumps(
            {
                "alarma": config.alarma,
                "tiempoOpen": config.tiempoOpen,
                "cardReadInterval": config.cardReadInterval,
                "isReadModeEnabled": config.isReadModeEnabled,
                "maxAttempts": config.maxAttempts,
            }
        ),
    )

@bot.message_handler(commands=["intentos"])
def telegram_intentos(message):
    config = Configuracion.objects().first()
    try:
        config.maxAttempts = int(message.text.split(" ")[1])
        config.save()
        bot.reply_to(message, "Se ha cambiado el número máximo de intentos a {}".format(config.maxAttempts))
        client.publish(
            TOPIC_CONF,
            json.dumps(
                {
                    "alarma": config.alarma,
                    "tiempoOpen": config.tiempoOpen,
                    "cardReadInterval": config.cardReadInterval,
                    "isReadModeEnabled": config.isReadModeEnabled,
                    "maxAttempts": config.maxAttempts,
                }
            ),
        )
    except:
        bot.reply_to(message, "Error, el comando es /intentos <intentos>")

@bot.message_handler(commands=["notificar"])
def telegram_notificar(message):
    global notify_when_open
    if notify_when_open:
        bot.reply_to(message, "Se ha desactivado la notificación al abrir la puerta")
        notify_when_open = False
    else:
        bot.reply_to(message, "Se ha activado la notificación al abrir la puerta")
        notify_when_open = True

@app.route("/", methods=["GET"])
def index():
    return render_template("index.html")


@app.route("/usuarios", methods=["GET", "POST"])
def usuarios():
    if request.method == "GET":
        users = Usuario.objects()
        return render_template("usuarios.html", users=users)
    elif request.method == "POST":
        nombre = request.form["nombre"]
        tag = request.form["tag"]
        user = Usuario.objects(tag=tag).first()
        if user is None:
            user = Usuario(nombre=nombre, tag=tag)
            user.save()
            return redirect(url_for("usuarios"))
        else:
            users = Usuario.objects()
            return render_template(
                "usuarios.html", users=users, error="Ya existe un usuario con ese tag"
            )


@app.route("/usuarios/delete/<tag>", methods=["GET"])
def delete_usuario(tag):
    user = Usuario.objects(tag=tag).first()
    user.delete()
    return redirect(url_for("usuarios"))


@app.route("/configuracion", methods=["GET", "POST"])
def configuracion():
    if request.method == "GET":
        conf_actual = Configuracion.objects().first()
        return render_template("configuracion.html", conf=conf_actual)
    elif request.method == "POST":
        print(request.form)
        conf_actual = Configuracion.objects().first()
        try:
            conf_actual.alarma = request.form["alarma"] == "on"
        except:
            conf_actual.alarma = False
        conf_actual.tiempoOpen = int(request.form["tiempoOpen"])
        conf_actual.cardReadInterval = int(request.form["cardReadInterval"])
        try:
            conf_actual.isReadModeEnabled = request.form["isReadModeEnabled"] == "on"
        except:
            conf_actual.isReadModeEnabled = False
        conf_actual.maxAttempts = int(request.form["maxAttempts"])
        conf_actual.save()

        client.publish(
            TOPIC_CONF,
            json.dumps(
                {
                    "alarma": conf_actual.alarma,
                    "tiempoOpen": conf_actual.tiempoOpen,
                    "cardReadInterval": conf_actual.cardReadInterval,
                    "isReadModeEnabled": conf_actual.isReadModeEnabled,
                    "maxAttempts": conf_actual.maxAttempts,
                }
            ),
        )
        return redirect(url_for("configuracion"))


@app.route("/abrir", methods=["GET"])
def abrir():
    client.publish(TOPIC_ABRIR, json.dumps({"OK": True}))

    if notify_when_open:
        bot.send_message(CANAL_ID, "Se ha abierto la puerta desde la web")

    query = (
        "INSERT INTO `puerta.aperturas` (timestamp,origen) VALUES ('{}','{}')".format(
            datetime.now(), "web"
        )
    )
    bigquery_client.query(query)
    return redirect(url_for("index"))


if __name__ == "__main__":
    bot_polling_thread = threading.Thread(target=bot.polling, daemon=True)
    bot_polling_thread.start()

    client.on_connect = on_connect
    client.on_message = on_message

    MQTT_BROKER = os.environ.get('MQTT_BROKER', 'mosquitto')
    #MQTT_BROKER = os.environ.get("MQTT_BROKER", "localhost")
    client.connect(MQTT_BROKER, 1883, 60)

    client.loop_start()

    port = int(os.environ.get("PORT", 5000))
    app.run(debug=False, host="0.0.0.0", port=port)
