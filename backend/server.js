const fs = require('fs')
const readline = require('readline')
const {google} = require('googleapis')
const https = require('https')
const express = require('express')
var app = express()

const LAMPAPICODE = 'INSERT API CODE HERE'
const STARTOFWORK = 8
const ENDOFWORK = 18
const REACTIONHOURS = 3
const SCOPES = ['https://www.googleapis.com/auth/calendar.readonly']
const TOKEN_PATH = 'token.json'
let calendarObjects = {}
let eventList = {}
let stressLevel = 0
let timeToNextEvent = -1
let standbyTime = 0
let eyeBrightness = 120
let lampState = 0
let brightnessLevel = 0.5
let oldBrightnessLevel = 0.5

fs.readFile('credentials.json', (err, content) => {
    if (err) return console.log('Error loading client secret file:', err)

    let userId = "user_01"
    authorize(JSON.parse(content),userId,getCalendarObject)
})

function authorize(credentials, userId, callback) {
    const {client_secret, client_id, redirect_uris} = credentials.installed
    const oAuth2Client = new google.auth.OAuth2(
        client_id, client_secret, redirect_uris[0])

    // Check if we have previously stored a token.
    fs.readFile(userId + "-" + TOKEN_PATH, (err, token) => {
        if (err) return getAccessToken(oAuth2Client, userId, callback)
        oAuth2Client.setCredentials(JSON.parse(token))
        callback(oAuth2Client,userId)
    })
}

function getAccessToken(oAuth2Client, userId, callback) {
    const authUrl = oAuth2Client.generateAuthUrl({
        access_type: 'offline',
        scope: SCOPES,
    })
    console.log('Authorize this app by visiting this url:', authUrl)
    const rl = readline.createInterface({
        input: process.stdin,
        output: process.stdout,
    })
    rl.question('Enter the code from that page here: ', (code) => {
        rl.close();
        oAuth2Client.getToken(code, (err, token) => {
        if (err) return console.error('Error retrieving access token', err)
        oAuth2Client.setCredentials(token)
        // Store the token to disk for later program executions
        fs.writeFile(userId + "-" + TOKEN_PATH, JSON.stringify(token), (err) => {
            if (err) return console.error(err)
            console.log('Token stored to', userId + "-" + TOKEN_PATH)
        })
        callback(oAuth2Client,userId)
        })
    })
}

function getCalendarObject(auth, userId) {
    calendarObjects[userId] = google.calendar({version: 'v3', auth})
    startScript(userId)
}

async function getEventData(userId, email) {
    let today = new Date()
    let endTime = new Date()
    endTime.setHours(endTime.getHours()+REACTIONHOURS,0,0,0)
    let upcomingEventsOfUser = []
  
    let err, res = await calendarObjects[userId].events.list({
		calendarId: 'primary',
		timeMin: today.toISOString(),
		timeMax: endTime.toISOString(),
		maxResults: 100,
		singleEvents: true,
		orderBy: 'startTime',
    })
    if (err) return console.log('The API returned an error: ' + err)
    const events = res.data.items
    if (events.length) {
      events.map((event, i) => {
        const start = event.start.dateTime || event.start.date
        const end = event.end.dateTime || event.end.date
        let dateStampEventStart = new Date(start)
        let dateStampEventEnd = new Date(end)
        let eventDuration = Math.floor((dateStampEventEnd.getTime() - dateStampEventStart.getTime())/1000/60)
        let accepted = 'no_choice_made'

        if (event.attendees && event.attendees.length > 0) {
          event.attendees.forEach(attendee => {
            if (attendee.email == email || attendee.self) {
				accepted = attendee.responseStatus
            }
          })
        }
  
        upcomingEventsOfUser.push({
			title: event.summary,
			start: dateStampEventStart,
			end: dateStampEventEnd,
			duration: eventDuration,
			attendees: event.attendees ? event.attendees.length : 0,
			isOnline: event.conferenceData ? true : false,
			isOrganizer: (event.organizer && event.organizer.email && event.organizer.email == email) || (event.organizer && event.organizer.self) ? true : false,
			accepted: accepted,
        })
      })
    } else {
      console.log('No upcoming events found.')
    }
  
    eventList[userId] = upcomingEventsOfUser
}

function calculateStressLevel(userId) {
	let totalDuration = 0 
	let tmpTime = new Date()
	let endOfReactionTimestamp = new Date()
	endOfReactionTimestamp.setHours(endOfReactionTimestamp.getHours()+REACTIONHOURS)
	let foundReminder = false
	let tmpTimeToNextEvent = -1
	eventList[userId].forEach(event => {
		if (tmpTimeToNextEvent < 0) {
			tmpTimeToNextEvent = Math.floor((event.start.getTime() - tmpTime.getTime())/1000/60)
		}
		if (event.end.getTime()<=endOfReactionTimestamp.getTime() && event.start.getTime() >= tmpTime.getTime()) {
			// event is completely within the reaction time -> count entire event
			//duration in min
			console.log("full event found " + event.title)
			totalDuration+=event.duration
		}
		else if (event.end.getTime() > endOfReactionTimestamp.getTime()) {
			console.log("partial event found in reactiontime " + event.title)
			totalDuration+=Math.floor((endOfReactionTimestamp.getTime() - event.start.getTime())/1000/60)
		}
		else {
			console.log("already ongoing event found " + event.title)
			totalDuration+=Math.floor((event.end.getTime() - tmpTime.getTime())/1000/60)
		}
		
		let differenceStarting = (event.start.getTime() - tmpTime.getTime())/1000
		console.log("test starting " + differenceStarting)
		if (differenceStarting >= 450  && differenceStarting <= 600) {
			console.log("Found event reminder")
			foundReminder = true
		}
	})

	if (tmpTimeToNextEvent < 0) {
		tmpTimeToNextEvent = -1
	}
	let date = new Date()
	let secondsUntilEndOfWorkDay = REACTIONHOURS*60*60
	if (date.getHours() >= ENDOFWORK) {
		return [-1,tmpTimeToNextEvent]
	}
	if (date.getHours() < STARTOFWORK) {
		return [-1,tmpTimeToNextEvent]
	}
	return [Math.floor(totalDuration*60/secondsUntilEndOfWorkDay*100) > 0 ? Math.floor(totalDuration*60/secondsUntilEndOfWorkDay*100) : 0, tmpTimeToNextEvent]
}
  
async function startScript(userId) {
	let email = await getUserEmail(userId)
  
	setInterval(async function(){
		await getEventData(userId, email)
		let newValuesCalendar = calculateStressLevel(userId)

		stressLevel = newValuesCalendar[0]
		timeToNextEvent = newValuesCalendar[1]
		if (stressLevel < 0) {
			standbyTime = 1
		} 
		else {
			standbyTime = 0
		}

		console.log("Current stress level: " + stressLevel)
		console.log("Time to next event: " + timeToNextEvent)
		console.log("Outside working hours: " + standbyTime)

		if (standbyTime) {
			lampState = -1
			setLightbulbOff()
		} 
		else if (timeToNextEvent <= 5 && timeToNextEvent >= 0) {
			// NOTIFICATION (250 = BLUE)
			lampState = 250
			setLightbulbHSL(lampState,1,brightnessLevel,2.0)
		}
		else {
			if (stressLevel > 100) {
				stressLevel = 100
			}
			// 100 = GREEN; 0 = RED
			lampState = 100-stressLevel
			setLightbulbHSL(lampState,1,brightnessLevel,2.0)
		}
		oldBrightnessLevel = brightnessLevel
	}, 30000)
}

async function getUserEmail(userId) {
	let err, res = await calendarObjects[userId].calendarList.list({})
	if (err) return console.log('The API returned an error: ' + err)
	const cal = res.data.items
	if (cal.length) {
	  console.log("user:" + userId + " // email:" + cal[0].id)
	  return cal[0].id
	}
	return null
}

function setLightbulbHSL(h, s, l, duration) {
	const data = JSON.stringify({
	  power:"on",
	  color:`hue:${h} saturation:${s} brightness:${l}`,
	  duration: duration,
	})
  
	const options = {
	  hostname: 'api.lifx.com',
	  port: 443,
	  path: '/v1/lights/all/state',
	  method: 'PUT',
	  headers: {
		'Content-Type': 'application/json',
		'Content-Length': data.length,
		'Authorization': 'Bearer ' + LAMPAPICODE,
	  }
	}
	const req = https.request(options, res => {
	  console.log(`statusCode: ${res.statusCode}`)
	})
  
	req.on('error', error => {
	  console.error(error)
	})
  
	req.write(data)
	req.end()
}
  
function setLightbulbOff() {
	const data = JSON.stringify({
	  power:"off",
	  duration: 2.0,
	})
  
	const options = {
	  hostname: 'api.lifx.com',
	  port: 443,
	  path: '/v1/lights/all/state',
	  method: 'PUT',
	  headers: {
		'Content-Type': 'application/json',
		'Content-Length': data.length,
		'Authorization': 'Bearer ' + LAMPAPICODE,
	  }
	}
  
	const req = https.request(options, res => {
	  console.log(`statusCode: ${res.statusCode}`)
	})
  
	req.on('error', error => {
	  console.error(error)
	})
  
	req.write(data)
	req.end()
}

app.get('/mattis', function (req, res) {
    res.end(eyeBrightness + ";" + stressLevel + ";" + timeToNextEvent + ";" + standbyTime)
})

app.get('/smart-cup', function (req, res) {
	var scale = req.query.scale
	var temperature = req.query.temperature
  
	var scaleNumber = 0.0
  
	if (Number.isNaN(Number.parseFloat(scale))) {
	  scaleNumber = 0.0
	}
	else {
	  scaleNumber = (-1 * Number.parseFloat(scale))-84
	}
  
	if (scaleNumber > 30) {
	  //empty standard compatible cup should be around 50 (most cups are a bit lighter, therefore -40)
	  let tmpBrightness = (scaleNumber-40)/50
  
	  if (tmpBrightness < 0.0) {
		tmpBrightness = 0.0
	  }
	  if (tmpBrightness > 0.8) {
		tmpBrightness = 0.8
	  }
	  brightnessLevel = (1.0 - tmpBrightness)*0.6
	  console.log("New brightness level: " + brightnessLevel)
	  if (Math.abs(brightnessLevel - oldBrightnessLevel) > 0.1) {
		if (lampState < 0) {
			setLightbulbOff()
		} 
		else {
			setLightbulbHSL(lampState,1,brightnessLevel,2.0)
		}
		oldBrightnessLevel = brightnessLevel
	  } 
	}
})
  
var server = app.listen(9085, function () {
    var host = server.address().address
    var port = server.address().port
    console.log("Example app listening at http://%s:%s", host, port)
})
  