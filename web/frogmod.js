// frogmod.js

var info_update_millis = 100000; // server info rarely changes
var players_update_millis = 2000;
var status_update_millis = 2000;
var admin = false;
var states = [ 'Alive', 'Dead', 'Spawning', 'Lagged', 'Editing', 'Spectator' ];

function ajaxCall(uri, cb) {
	var xhr;
	if(window.XMLHttpRequest) { // code for IE7+, Firefox, Chrome, Opera, Safari
		xhr=new XMLHttpRequest();
	} else { // code for IE6, IE5
		xhr=new ActiveXObject("Microsoft.XMLHTTP");
	}
	xhr.onreadystatechange = cb;
	xhr.open('GET', uri + '?' + Date(), true);
	xhr.send();
}

function format_time(t) {
	var seconds = Math.floor(t / 1000) % 60;
	if(seconds < 10) seconds = '0'+seconds; // zero pad x_x
	var minutes = Math.floor(t / 60000) % 60;
	if(minutes < 10) minutes = '0'+minutes;
	var hours = Math.floor(t / 3600000) % 24;
	if(hours < 10) hours = '0' + hours;
	var days = Math.floor(t / 86400000);
	return (days > 0 ? days + 'd ': '') + hours + ':' + minutes + ':' + seconds;
}

function update_info() {
	ajaxCall('/info', function() {
		if(this.readyState == 4) {
			var div = document.getElementById('info');
			if(div) {
				if(this.status == 200) {
					var st = eval('('+this.responseText+')');
					if(st) {
						var h1 = document.getElementById('title');
						if(h1 && st.serverdesc && st.serverdesc != '') h1.innerHTML = st.serverdesc + (admin?' admin':'') + ' console';

						var html = new Array();
						html.push('<ul>');
						html.push('<li>Server name: '+st.serverdesc+'</li>');
						html.push('<li>Message of the day: '+st.servermotd+'</li>');
						html.push('<li>Max players: '+st.maxclients+'</li>');
						html.push('</ul>');
						div.innerHTML = html.join('');
					} else div.innerHTML = 'Could not parse response';
				} else div.innerHTML = 'Error ' + (this.status ? this.status : '(unreachable)');
				setTimeout(update_status, status_update_millis);
			}
		}
	});
}

function update_status() {
	ajaxCall('/status', function() {
		if(this.readyState == 4) {
			var div = document.getElementById('status');
			if(div) {
				if(this.status == 200) {
					var st = eval('('+this.responseText+')');
					if(st) {
						var html = new Array();
						html.push('<ul>');
						html.push('<li>Uptime: '+format_time(st.totalmillis)+'</li>');
						html.push('<li>Mastermode: '+st.mastermodename+' ('+st.mastermode+')</li>');
						html.push('<li>Game mode: '+st.gamemodename+' ('+st.gamemode+')</li>');
						html.push('<li>Map: '+(st.map?'<b>'+st.map+'</b>':'<i>No map</i>')+'</li>');
						html.push('</ul>');
						div.innerHTML = html.join('');
					} else div.innerHTML = 'No players on the server.';
				} else div.innerHTML = 'Error ' + (this.status ? this.status : '(unreachable)');
				setTimeout(update_status, status_update_millis);
			}
		}
	});
}

function update_players_cb(xhr) {
	if(xhr.readyState == 4) {
		var div = document.getElementById('players');
		if(div) {
			if(xhr.status == 200) {
				var players = eval('(' + xhr.responseText + ')');
				if(players && players.length > 0) {
					var html = new Array();
					html.push('<table><tr><th>Cn</th><th>Name</th><th>Team</th><th>Status</th>');
					html.push('<th>Ping</th><th>Frags</th><th>Deaths</th><th>Teamkills</th><th>Shotdamage</th>');
					html.push('<th>Damage</th><th>Effectiveness</th>');
					html.push('<th>Uptime</th>'+(admin?'<th>Actions</th>':'')+'</tr>');
					for(p in players) {
						html.push('<tr class="privilege'+players[p].privilege+'">');
						html.push('<td>'+players[p].clientnum+'</td>');
						html.push('<td>'+players[p].name+'</td>');
						html.push('<td>'+players[p].team+'</td>');
						html.push('<td>'+states[players[p].state]+'</td>');
						html.push('<td>'+players[p].ping+'</td>');
						html.push('<td>'+players[p].frags+'</td>');
						html.push('<td>'+players[p].deaths+'</td>');
						html.push('<td>'+players[p].teamkills+'</td>');
						html.push('<td>'+players[p].shotdamage+'</td>');
						html.push('<td>'+players[p].damage+'</td>');
						html.push('<td>'+players[p].effectiveness+'</td>');
						html.push('<td>'+format_time(players[p].connectmillis)+'</td>');
						if(admin) {
							html.push('<td><a href="?kick='+players[p].clientnum+'" title="Kick">[K]</a> ');
							if(players[p].state == 5)
								html.push('<a href="?unspec='+players[p].clientnum+'" title="Unspec">[U]</a> ');
							else
								html.push('<a href="?spec='+players[p].clientnum+'" title="Spec">[S]</a> ');
							if(players[p].privilege)
								html.push('<a href="?takemaster=true" title="Take master">[T]</a> ');
							else
								html.push('<a href="?givemaster='+players[p].clientnum+'" title="Give master">[M]</a> ');
							html.push('</td>');
						}
						html.push('</tr>');
					}
					html.push('</table>');
					div.innerHTML = html.join('');
				} else div.innerHTML = 'No players on the server.';
			} else div.innerHTML = 'Error ' + (xhr.status ? xhr.status : '(unreachable)');
			setTimeout(update_players, players_update_millis);
		}
	}
}

function update_players(admin) {
	ajaxCall('/players', admin ? function() { update_players_cb(this, true); } : function() { update_players_cb(this, false); });
}

function init() {
	update_info();
	update_status();
	update_players();
}
