while true; do
  timestamp=$(date +%s)
  humidity=$(curl --request GET --url esp32.local/soil)
  hum=$(curl --request GET --url esp32.local/hum)
  temp=$(curl --request GET --url esp32.local/temp)
  echo "$timestamp\t$humidity\t$hum\t$temp"  >> ./data.txt
  sleep 300
done
