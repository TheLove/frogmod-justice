// frogmod.js

var info_update_millis = 100000; // server info rarely changes
var players_update_millis = 2000;
var status_update_millis = 2000;
var result_millis = 5000;
var admin = false;
var states = [ 'Alive', 'Dead', 'Spawning', 'Lagged', 'Editing', 'Spectator' ];
var icons = [ 'fixit.png', 'ironsnout.png', 'ogro.png', 'inky.png', 'cannon.png' ];

function ajaxCall(uri, cb) {
	var xhr;
	if(window.XMLHttpRequest) { // code for IE7+, Firefox, Chrome, Opera, Safari
		xhr=new XMLHttpRequest();
	} else { // code for IE6, IE5
		xhr=new ActiveXObject("Microsoft.XMLHTTP");
	}
	xhr.onreadystatechange = cb;
	xhr.open('GET', uri + (uri.indexOf('?') > -1 ? '&' : '?') + Date(), true);
	xhr.send();
}

function format_time(t, show_hours) {
	var seconds = Math.floor(t / 1000) % 60;
	if(seconds < 10) seconds = '0'+seconds; // zero pad x_x
	var minutes = Math.floor(t / 60000) % 60;
	if(minutes < 10) minutes = '0'+minutes;
	var hours = Math.floor(t / 3600000) % 24;
	if(hours < 10) hours = '0' + hours;
	var days = Math.floor(t / 86400000);
	return (days > 0 ? days + 'd ': '') + (show_hours?hours+':':(parseInt(hours)>0?hours+':':'')) + minutes + ':' + seconds;
}

function escapeHtml(str) {
	str = str.replace('<', '&lt;');
	str = str.replace('>', '&gt;');

	return str;
}

function nl2br(str) {
	return str.replace(/\n/g, "<br />\n");
}

function autoUrls(str) {
	if(!str) return;
//	urls = str.match(/(ftp|http|https):\/\/(\w+:{0,1}\w*@)?([^\s<>]+)(:[0-9]+)?(\/|\/([\w#!:.?+=&%@!\-\/]))?/);
//	for(u in urls) str = str.replace(urls[u][3], '<a href="'+urls[u][3]+'">'+escapeHtml(urls[u][3])+'</a>');
	return str;
}

//! written by quaker66:
//! Let's convert a Cube string colorification into proper HTML spans
//! Accepts just one argument, returns the html string.

function convert_cube_string(str) {
    var tmp = escapeHtml(str); // some temp we'll return later
    var found = false; // have we found some colorz??!
    var pos = tmp.indexOf('\f'); // first occurence of \f
    while (pos != -1) { // loop till there is 0 occurs.
        var color = parseInt(tmp.substr(pos + 1, 1));
        if (found) { // if we've found something before, close the span on > 6 or any character, or close+create new on 0-6
            if (color <= 6 && color >= 0) { // yay! color exists. It means we'll want to close last span.
                tmp = tmp.replace(/\f[0-6]/, "</span><span class=\"color" + tmp.substr(pos + 1, 1) + "\">");
            } else { // There is no color. It means the num is higher than 6 (or any char).
                tmp = tmp.replace(/\f./, "</span>");
                found = false; // pretend we've never found anything
            }
        } else { // if it's first occurence and its num is bigger than 6 (or any char), simply ignore.
            if (color <= 6 && color >= 0) { // this means the num is 0-6. In that case, create our first span.
                tmp = tmp.replace(/\f[0-6]/, "<span class=\"color" + tmp.substr(pos + 1, 1) + "\">");
                found = true; // yay! we've found a color! (or again?)
            }
        }
        pos = tmp.indexOf('\f', pos + 1); // move to next position to feed while
    }
    // if we've found anything lately and didn't close it with \f > 6 (or \fCHAR), let's do it at the end
    if (found) tmp = tmp.replace(/$/, "</span>");

    // we can finally return our html string.
    return tmp;
}

result_timeout = null;
function set_result(str) {
	if(result_timeout) clearTimeout(result_timeout);
	var div = document.getElementById('result');
	if(div) {
		div.style.display = 'block';
		div.innerHTML = str ? nl2br(convert_cube_string(str)) : '<i style="color: darkGreen">Empty result</i>';
		result_timeout = setTimeout(function() { document.getElementById('result').style.display = 'none'; }, result_millis);
	}
}

function execute(c) {
	ajaxCall('?command='+escape(c), function() {
		if(this.readyState == 4 && this.status == 200) {
			set_result(this.responseText);
		}
	});
	update_info();
	update_status();
	update_players();
	return false;
}

var info_timeout = null;
function update_info() {
	if(info_timeout) { clearTimeout(info_timeout); info_timeout = null; }
	ajaxCall('/info', function() {
		if(this.readyState == 4) {
			var div = document.getElementById('info');
			if(div) {
				if(this.status == 200) {
					var st = eval('('+this.responseText+')');
					if(st) {
						if(st.serverdesc && st.serverdesc != '') {
							var h1 = document.getElementById('title');
							if(h1) h1.innerHTML = convert_cube_string(st.serverdesc) + (admin?' admin':'') + ' console';
							document.title = st.serverdesc.replace(/\f./g, '') + (admin?' admin':'') + ' console';
						}

						var html = new Array();
						html.push('<ul>');
						html.push('<li>Server name: '+autoUrls(convert_cube_string(st.serverdesc))+'</li>');
						html.push('<li>Message of the day: '+autoUrls(convert_cube_string(st.servermotd))+'</li>');
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

status_timeout = null;
function update_status() {
	if(status_timeout) { clearTimeout(status_timeout); status_timeout = null; }
	ajaxCall('/status', function() {
		if(this.readyState == 4) {
			var div = document.getElementById('status');
			if(div) {
				if(this.status == 200) {
					var st = eval('('+this.responseText+')');
					if(st) {
						var html = new Array();
						html.push('<ul>');
						html.push('<li>Uptime: '+format_time(st.totalmillis, true)+'</li>');
						html.push('<li>Mastermode: '+st.mastermodename+' ('+st.mastermode+')</li>');
						html.push('<li>Game mode: '+st.gamemodename+' ('+st.gamemode+')</li>');
						html.push('<li>Game time: '+format_time(st.gamemillis)+' / '+format_time(st.gamelimit)+'</li>');
						html.push('<li>Map: '+(st.map?'<b>'+st.map+'</b>':'<i>No map</i>')+'</li>');
						html.push('</ul>');
						div.innerHTML = html.join('');
						var modeselect = document.getElementById('modeselect'); if(modeselect) modeselect.value = st.gamemode;
						var mastermodeselect = document.getElementById('mastermodeselect'); if(mastermodeselect) mastermodeselect.value = st.mastermode;
					} else div.innerHTML = 'No players on the server.';
				} else div.innerHTML = 'Error ' + (this.status ? this.status : '(unreachable)');
				status_timeout = setTimeout(update_status, status_update_millis);
			}
		}
	});
}

function player_icon(model) {
	if(model < 0 || model >= icons.length) return ''; // wot playermodel is that?!?
	return '<img title="'+icons[model].replace('.png', '')+'" src="'+icons[model]+'">';
}

players_timeout = null;
function update_players_cb(xhr) {
	if(xhr.readyState == 4) {
		var div = document.getElementById('players');
		if(div) {
			if(xhr.status == 200) {
				var players = eval('(' + xhr.responseText + ')');
				if(players && players.length > 0) {
					var html = new Array();
					html.push('<table><tr><th>Cn</th><th>Name</th>');
					if(admin) html.push('<th>IP</th>'); else html.push('<th>Country</th>');
					html.push('<th>Team</th><th>Status</th>');
					html.push('<th>Ping</th><th>Frags</th><th>Deaths</th><th>Teamkills</th><th>Shotdamage</th>');
					html.push('<th>Damage</th><th>Effectiveness</th>');
					html.push('<th>Uptime</th>'+(admin?'<th>Actions</th>':'')+'</tr>');
					for(p in players) {
						html.push('<tr'+(players[p].state == 5 ? ' class="spec"': (players[p].state == 1 ? ' class="dead"' :''))+'>');
						html.push('<td>'+players[p].clientnum+'</td>');
						html.push('<td class="privilege'+players[p].privilege+'">' + player_icon(players[p].playermodel) + ' ' + escapeHtml(players[p].name) + '</td>');
						html.push('<td>');
						if(admin) html.push(players[p].hostname + ' (');
						if(players[p].country) html.push(players[p].country);
						if(admin) html.push(')');
						html.push('</td>');
						html.push('<td>'+players[p].team+'</td>');
						html.push('<td>'+states[players[p].state]+'</td>');
						html.push('<td>'+players[p].ping+'</td>');
						html.push('<td>'+players[p].frags+'</td>');
						html.push('<td>'+players[p].deaths+'</td>');
						html.push('<td>'+players[p].teamkills+'</td>');
						html.push('<td>'+players[p].shotdamage+'</td>');
						html.push('<td>'+players[p].damage+'</td>');
						html.push('<td>'+players[p].effectiveness+'</td>');
						html.push('<td>'+(players[p].clientnum < 128 ? format_time(players[p].connectmillis) : '')+'</td>');
						if(admin) {
							html.push('<td><a href="?command=kick+'+players[p].clientnum+'" onclick="return execute(&quot;kick '+players[p].clientnum+'&quot;)" title="Kick"><img src="kick.png" alt="[K]"></a> ');
							if(players[p].state == 5)
								html.push('<a href="?command=spectator+0+'+players[p].clientnum+'" onclick="return execute(&quot;spectator 0 '+players[p].clientnum+'&quot;);" title="Unspec"><img src="unspec.png" alt="[U]"</a> ');
							else
								html.push('<a href="?command=spectator+1+'+players[p].clientnum+'" onclick="return execute(&quot;spectator 1 '+players[p].clientnum+'&quot;);" title="Spec"><img src="spec.png" alt="[S]"></a> ');
							if(players[p].privilege)
								html.push('<a href="?command=takemaster" title="Take master" onclick="return execute(&quot;takemaster&quot;);"><img src="takemaster.png" alt="[T]"</a> ');
							else
								html.push('<a href="?command=givemaster+'+players[p].clientnum+'" onclick="return execute(&quot;givemaster '+players[p].clientnum+'&quot;);" title="Give master"><img src="givemaster.png" alt="[M]"></a> ');
							html.push('</td>');
						}
						html.push('</tr>');
					}
					html.push('</table>');
					div.innerHTML = html.join('');
				} else div.innerHTML = 'No players on the server.';
			} else div.innerHTML = 'Error ' + (xhr.status ? xhr.status : '(unreachable)');
			players_timeout = setTimeout(update_players, players_update_millis);
		}
	}
}

function update_players(admin) {
	if(players_timeout) { clearTimeout(players_timeout); players_timeout = null; }
	ajaxCall('/players', admin ? function() { update_players_cb(this, true); } : function() { update_players_cb(this, false); });
}

function init() {
	update_info();
	update_status();
	update_players();
}
