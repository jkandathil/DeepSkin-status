function doGet(e) {
    // --- HELPER: GET SHEET ---
    function getDeviceSheet(devName) {
        if (!devName) return null;
        var fileName = "Log_" + devName;
        var folders = DriveApp.getFoldersByName("werable_watch_deepskin");
        if (!folders.hasNext()) return null;
        var folder = folders.next();
        var files = folder.getFilesByName(fileName);
        if (files.hasNext()) return SpreadsheetApp.openById(files.next().getId());
        return null;
    }

    // --- HANDLE BATTERY CHECK ---
    if (e && e.parameter && e.parameter.action === "getBattery") {
        var devName = e.parameter.device;
        var callback = e.parameter.callback; // JSONP Callback Name

        var result = { battery: 0, status: "No Data", timestamp: "" };

        var ss = getDeviceSheet(devName);
        if (ss) {
            var dataSheet = ss.getSheetByName("Data");
            var lastRow = dataSheet.getLastRow();
            if (lastRow >= 2) {
                var values = dataSheet.getRange(lastRow, 1, 1, 4).getValues()[0];
                result = {
                    timestamp: values[0],
                    device: values[1],
                    status: values[2],
                    battery: values[3]
                };
            }
        }

        var json = JSON.stringify(result);

        // JSONP Support
        if (callback) {
            return ContentService.createTextOutput(callback + "(" + json + ")").setMimeType(ContentService.MimeType.JAVASCRIPT);
        } else {
            return ContentService.createTextOutput(json).setMimeType(ContentService.MimeType.JSON);
        }
    }

    return ContentService.createTextOutput("DeepSkin Server Running");
}

function doPost(e) {
    var folderName = "werable_watch_deepskin";

    function getDeviceSheet(devName) {
        if (!devName) devName = "Unknown_Device";
        var fileName = "Log_" + devName;
        var folder;
        var folders = DriveApp.getFoldersByName(folderName);
        if (folders.hasNext()) folder = folders.next();
        else folder = DriveApp.createFolder(folderName);
        var ss;
        var files = folder.getFilesByName(fileName);
        if (files.hasNext()) ss = SpreadsheetApp.openById(files.next().getId());
        else {
            ss = SpreadsheetApp.create(fileName);
            var file = DriveApp.getFileById(ss.getId());
            folder.addFile(file);
            DriveApp.getRootFolder().removeFile(file);
        }
        var dataSheet = ss.getSheetByName("Data");
        if (!dataSheet) {
            dataSheet = ss.insertSheet("Data");
            dataSheet.appendRow(["Timestamp", "Device", "Status", "Battery", "Steps", "MaxG", "Fall",
                "CTX_User", "CTX_DeviceID", "CTX_Event", "CTX_Note", "Server_Time"]);
        }
        var configSheet = ss.getSheetByName("Config");
        if (!configSheet) {
            configSheet = ss.insertSheet("Config");
            if (configSheet.getLastRow() < 2) {
                configSheet.getRange("A1:E1").setValues([["User", "DeviceID", "Event", "Note", "Event_TS"]]);
                configSheet.getRange("A2:E2").setValues([["", devName, "", "", ""]]);
            }
        }
        return ss;
    }

    if (!e || !e.postData) return ContentService.createTextOutput("Manual Run: OK");

    var content = e.postData.contents;
    var isWebApp = false;
    var jsonPayload = {};
    try {
        jsonPayload = JSON.parse(content);
        if (jsonPayload.user && jsonPayload.device) isWebApp = true;
    } catch (err) { isWebApp = false; }

    if (isWebApp) {
        var devName = jsonPayload.device;
        var ss = getDeviceSheet(devName);
        var configSheet = ss.getSheetByName("Config");
        configSheet.getRange("A2:E2").setValues([[
            jsonPayload.user, devName, jsonPayload.event, jsonPayload.context, jsonPayload.timestamp
        ]]);
        return ContentService.createTextOutput("Event Registered for " + devName);
    }

    else {
        var lines = content.split("\n");
        var firstLine = lines[0].trim();
        if (firstLine.length < 5) return ContentService.createTextOutput("Empty Data");
        var firstCols = firstLine.split(",");
        var devName = firstCols[1] || "Unknown";

        var ss = getDeviceSheet(devName);
        var dataSheet = ss.getSheetByName("Data");
        var configSheet = ss.getSheetByName("Config");
        var conf = configSheet.getRange("A2:E2").getValues()[0];
        var evtUser = conf[0], evtDev = conf[1], evtName = conf[2], evtNote = conf[3], evtTimeStr = conf[4];
        var evtTime = (evtTimeStr) ? new Date(evtTimeStr).getTime() : 0;

        var grid = [];
        var serverTime = new Date().toISOString();
        var matchFound = false;

        for (var i = 0; i < lines.length; i++) {
            var line = lines[i].trim();
            if (line.length < 5) continue;
            var cols = line.split(",");
            var rowTime = new Date(cols[0].replace(" ", "T")).getTime();
            var cUser = "", cDev = "", cEvt = "", cNote = "";

            if (evtTime > 0 && Math.abs(rowTime - evtTime) < 3500) {
                cUser = evtUser; cDev = evtDev; cEvt = evtName; cNote = evtNote;
                matchFound = true;
            }
            var row = [
                cols[0], cols[1], cols[2], cols[3], cols[4], cols[5], cols[6],
                cUser, cDev, cEvt, cNote, serverTime
            ];
            grid.push(row);
        }

        if (grid.length > 0) dataSheet.getRange(dataSheet.getLastRow() + 1, 1, grid.length, grid[0].length).setValues(grid);
        var lastRowTime = new Date(lines[lines.length - 2].split(",")[0].replace(" ", "T")).getTime();
        if (matchFound || (evtTime > 0 && evtTime < lastRowTime - 10000)) {
            configSheet.getRange("A2:E2").setValues([["", devName, "", "", ""]]);
        }
        return ContentService.createTextOutput("Saved. Match: " + matchFound);
    }
}

function testSetup() { Logger.log("OK"); }
