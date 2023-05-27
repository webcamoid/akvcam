function Component()
{
    let paths = installer.environmentVariable("PATH").split(":");
    let kernelVersion = installer.execute("uname", ["-r"])[0].trim();
    let linuxSources = "/lib/modules/" + kernelVersion + "/build";
    let cmds = ["dkms", "gcc", "kmod", "make"]

    for (;;) {
        let missing_dependencies = [];

        for (let i in cmds)
            if (installer.findPath(cmds[i], paths).length < 1)
                missing_dependencies.push(cmds[i]);

        if (!installer.fileExists(linuxSources + "/include/generated/uapi/linux/version.h"))
            missing_dependencies.push("linux-headers");

        if (missing_dependencies.length < 1)
            break;

        let result = QMessageBox.information("missing_dependencies",
                                             "Missing dependencies",
                                             "The following dependencies are missing: "
                                             + missing_dependencies.join(", ")
                                             + ". Install them and try again",
                                             QMessageBox.Retry | QMessageBox.Close);

        if (result == QMessageBox.Close) {
            QMessageBox.critical("missing_dependencies.dialog_close",
                                 "Error",
                                 "Aborting driver installation due to unmeet dependencies.",
                                 QMessageBox.Ok);
            installer.setCanceled();

            break;
        }
    }
}

Component.prototype.beginInstallation = function()
{
    component.beginInstallation();
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    // Create a symlink to the sources.
    component.addElevatedOperation("Execute",
                                   "ln",
                                   "-s",
                                   "@TargetDir@/src",
                                   "/usr/src/@Name@-@Version@",
                                   "UNDOEXECUTE",
                                   "rm",
                                   "-f",
                                   "/usr/src/@Name@-@Version@");

    // Run DKMS.
    component.addElevatedOperation("Execute",
                                   "dkms",
                                   "install",
                                   "@Name@/@Version@",
                                   "UNDOEXECUTE",
                                   "dkms",
                                   "remove",
                                   "@Name@/@Version@",
                                   "--all");
}
