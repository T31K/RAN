
CREATE PROCEDURE [dbo].[sp_UserAttendLog ]
	@nUserNum	int

AS
DECLARE
		@error_var int, 
		@rowcount_var int	

	SET NOCOUNT ON
Select (MSum-MS) As M, (FSum-FS) As F
From
(
	Select isnull(Sum(M),0) As MSum, isnull(Sum(F),0) As FSum, isnull(Sum(MS),0) As MS, isnull(Sum(FS),0) As FS
	From
	(
	Select ChaClass
	,
	Case ChaClass
	When 1 Then 1
	When 2 Then 1
	When 256 Then 1
	When 512 Then 1
	Else 0
	End As M
	,
	Case ChaClass
	When 4 Then 1
	When 8 Then 1
	When 64 Then 1
	When 128 Then 1
	Else 0
	End As F
	,
	Case ChaClass
	When 16 Then 
		Case ChaDeleted
		When 4 Then 0
		Else 1
		End
	Else 0
	End As MS
	,
	Case ChaClass
	When 32 Then 
		Case ChaDeleted
		When 4 Then 0
		Else 1
		End
	Else 0
	End As FS
	From ChaInfo
	) As t
) As tt

