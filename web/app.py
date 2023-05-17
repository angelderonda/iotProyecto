from flask import Flask, render_template, request, redirect
from pymongo import MongoClient

app = Flask(__name__)

# Conexión a la base de datos
client = MongoClient('localhost', 27017)
db = client['iot']
collection = db['usuarios']

# Página de inicio
@app.route('/')
def index():
    return render_template('index.html')

# Registro de usuarios
@app.route('/registro', methods=['POST'])
def registro():
    nombre = request.form['nombre']
    rfid = request.form['rfid']

    # Insertar usuario en la base de datos
    collection.insert_one({'nombre': nombre, 'rfid': rfid})

    # Redireccionar a la página de inicio
    return redirect('/')

# Listar usuarios
@app.route('/lista')
def lista():
    usuarios = collection.find()
    return render_template('lista.html', usuarios=usuarios)

if __name__ == '__main__':
    app.run(debug=True)
    