var newest_version_id = -1;
var current_version_id = -1;

function checkForNewRelease(prerelease) {
    if(prerelease == true) {
        $.ajax({
            url: "https://api.github.com/repos/tpanajott/ESP32_IR_AC_Controller/releases?accept=application/vnd.github.v3+json&per_page=1",
            dataType: 'json',
            async: false,
            success: function(data) {
                newest_version_id = data[0]["id"]
            },
            fail: function(data) {
                alert("Failed to retrive update information. Check internet connection.");
            }
        });
    } else {
        $.ajax({
            url: "https://api.github.com/repos/tpanajott/ESP32_IR_AC_Controller/releases/latest?accept=application/vnd.github.v3+json",
            dataType: 'json',
            async: false,
            success: function(data) {
                newest_version_id = data[0]["id"]
            },
            fail: function(data) {
                alert("Failed to retrive update information. Check internet connection.");
            }
        });
    }

    if(newest_version_id == -1) {
        $("#latest_release_id").html("UNKNOWN");
    } else {
        $("#latest_release_id").html(newest_version_id);
        if(current_version_id == newest_version_id) {
            $("#latest_release_id").addClass("is-light");
        } else {
            // A new release is available.
            console.log("A new release is available: " + newest_version_id);
            $("#latest_release_id").addClass("is-success");
            $("#updateButton").prop("disabled", false);
        }
    }
}

function getCurrentlyInstalledRelease() {
    $.ajax({
        url: "/status",
        dataType: 'json',
        async: false,
        success: function(data) {
            if(data["current_version_id"] == -1) {
                $("#current_version_id").html("UNKNOWN");
            } else {
                $("#current_version_id").html(data["current_version_id"]);
            }
            current_version_id = data["current_version_id"];
        },
        fail: function(data) {
            alert("Failed to get current version. Check connection to device!");
        }
    });
}

jQuery(document).ready(function(){
    getCurrentlyInstalledRelease(); 
    checkForNewRelease(true);
});