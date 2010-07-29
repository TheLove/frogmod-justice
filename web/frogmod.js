var players_update_millis = 2000;
var status_update_millis = 2000;

function ajaxCall(uri, cb) {
	var xhr;
	if(window.XMLHttpRequest) { // code for IE7+, Firefox, Chrome, Opera, Safari
		xhr=new XMLHttpRequest();
	} else { // code for IE6, IE5
		xhr=new ActiveXObject("Microsoft.XMLHTTP");
	}
	xhr.onreadystatechange = cb;
	xhr.open('GET', uri, true);
	xhr.send();
}

function format_time(t) {
	var seconds = Math.floor(t / 1000) % 60;
	if(seconds < 10) seconds = '0'+seconds; // zero pad x_x
	var minutes = Math.floor(t / 60000) % 60;
	if(minutes < 10) minutes = '0'+minutes;
	var hours = Math.floor(t / 360000) % 24;
	if(hours < 10) hours = '0' + hours;
	var days = Math.floor(t / 86400000);
	return (days > 0 ? days + 'd ': '') + hours + ':' + minutes + ':' + seconds;
}

function update_players() {
	ajaxCall('/players', function() {
		if(this.readyState == 4 && this.status == 200) {
			var div = document.getElementById('players');
			if(div) {
				var players = eval('(' + this.responseText + ')');
				if(players && players.length > 0) {
					var html = new Array();
					html.push('<table><tr><th>Cn</th><th>Name</th><th>Team</th><th>Ping</th><th>Uptime</th></tr>');
					for(p in players) {
						html.push('<tr class="privilege'+players[p].privilege+'"><td>'+players[p].clientnum+'</td><td>'+players[p].name+'</td><td>'+players[p].team+'</td><td>'+players[p].ping+'</td><td>'+format_time(players[p].connectmillis)+'</td></tr>');
					}
					html.push('</table>');
					div.innerHTML = html.join('');
				} else div.innerHTML = 'No players on the server.';
			}
			setTimeout(update_players, players_update_millis);
		}
	});
}

function update_status() {
	ajaxCall('/status', function() {
		if(this.readyState == 4 && this.status == 200) {
			var div = document.getElementById('status');
			if(div) {
				var st = eval('('+this.responseText+')');
				if(st) {
					var html = new Array();
					html.push('<ul>');
					html.push('<li>Uptime: '+format_time(st.totalmillis)+'</li>');
					html.push('<li>Mastermode: '+st.mastermodename+' ('+st.mastermode+')</li>');
					html.push('<li>Game mode: '+st.gamemodename+' ('+st.gamemode+')</li>');
					html.push('<li>Map: '+(st.map?'<b>'+st.map+'</b>':'<i>No map</i>')+'</li>');
					html.push('<li>Max players: '+st.maxclients+'</li>');
					html.push('</ul>');
					div.innerHTML = html.join('');
				} else div.innerHTML = 'No players on the server.';
			}
			setTimeout(update_status, status_update_millis);
		}
	});
}

function init() {
	update_players();
	update_status();
}

