



CREATE VIEW [dbo].[viewLogHackProgram]
AS
SELECT   TOP 100 PERCENT dbo.LogHackProgram.HackNum, 
                dbo.LogHackProgram.UserNum, dbo.LogHackProgram.ChaNum, 
                dbo.LogHackProgram.SGNum, dbo.LogHackProgram.SvrNum, 
                dbo.LogHackProgram.HackProgramNum, dbo.LogHackProgram.HackDate, 
                dbo.LogHackProgram.HackComment, 
                dbo.HackProgramList.HackProgramName
FROM      dbo.LogHackProgram LEFT OUTER JOIN
                dbo.HackProgramList ON 
                dbo.LogHackProgram.HackProgramNum = dbo.HackProgramList.HackProgramNum



