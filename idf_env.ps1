# Atalho para ativar o ambiente ESP-IDF v5.4 neste terminal.
# Uso (no terminal do VS Code, dentro da pasta do projeto):
#   . .\idf_env.ps1
# (repare no ponto + espaco no inicio -- isso e "dot-source", necessario)

$env:PATH = "C:\Users\marce\.espressif\python_env\idf5.4_py3.11_env\Scripts;" + $env:PATH   # python que funciona
. "C:\esp\v5.4\export.ps1"                              # carrega idf.py + compilador
Write-Host ""
Write-Host "Ambiente ESP-IDF pronto. Agora rode:  idf.py -p COM6 flash monitor" -ForegroundColor Green
